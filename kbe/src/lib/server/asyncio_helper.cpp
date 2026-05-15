#include "asyncio_helper.h"
#include "serverconfig.h"
#include "pyscript/script.h"
#include "network/event_dispatcher.h"

#include <vector>

namespace KBEngine {

namespace
{
// 主线程持有 asyncio 模块，避免每次提交协程时重复 import。
PyObject* g_asyncioModule = NULL;

// 主线程持有唯一的 asyncio event loop，不再创建独立 Python 调度线程。
PyObject* g_loop = NULL;

// 保存未完成的 Task 强引用，防止协程运行过程中被 Python GC 回收。
std::vector<PyObject*> g_tasks;

// shutdown 期间拒绝新协程，避免关闭 loop 时又塞入新的任务。
bool g_shuttingDown = false;

// 底层dispatcher timer句柄：绕开ScriptTimers，避免asyncio调度频率被game tick量化。
TimerHandle g_timerHandle;

// 每次KBEngine timer触发时，最多连续推进asyncio多少轮，避免ready队列过长时占满主循环。
const int ASYNCIO_MAX_PUMP_ITERATIONS = 64;

// 每次KBEngine timer触发时，至少推进几轮；aiohttp从accept到handler进入await通常需要多轮call_soon传递。
const int ASYNCIO_MIN_PUMP_ITERATIONS = 8;

// 每次KBEngine timer触发时，最多占用约2ms，防止HTTP/async回调过多时拖慢游戏tick。
const uint64 ASYNCIO_MAX_PUMP_STAMPS = 2;

bool ensureLoop()
{
	// 如果 loop 已经创建，直接复用。
	if (g_loop != NULL)
		return true;

	// 导入 Python 标准库 asyncio。
	g_asyncioModule = PyImport_ImportModule("asyncio");
	if (g_asyncioModule == NULL)
	{
		SCRIPT_ERROR_CHECK();
		return false;
	}

	// 创建一个新的事件循环；这个 loop 只在 KBEngine 主线程被 pump。
	g_loop = PyObject_CallMethod(g_asyncioModule, const_cast<char*>("new_event_loop"), const_cast<char*>(""));
	if (g_loop == NULL)
	{
		SCRIPT_ERROR_CHECK();
		Py_CLEAR(g_asyncioModule);
		return false;
	}

	// 把 loop 设置为当前线程默认 loop，兼容 asyncio.get_event_loop()。
	PyObject* pyRet = PyObject_CallMethod(g_asyncioModule, const_cast<char*>("set_event_loop"), const_cast<char*>("O"), g_loop);
	if (pyRet == NULL)
	{
		SCRIPT_ERROR_CHECK();
		Py_CLEAR(g_loop);
		Py_CLEAR(g_asyncioModule);
		return false;
	}

	Py_DECREF(pyRet);

	// loop 初始化成功后，允许正常提交任务。
	g_shuttingDown = false;
	return true;
}

Py_ssize_t readySize()
{
	// 读取asyncio内部ready队列长度，用来判断是否还有马上可执行的回调。
	PyObject* pyReady = PyObject_GetAttrString(g_loop, "_ready");
	if (pyReady == NULL)
	{
		PyErr_Clear();
		return 0;
	}

	Py_ssize_t size = PyObject_Length(pyReady);
	Py_DECREF(pyReady);

	if (size < 0)
	{
		PyErr_Clear();
		return 0;
	}

	return size;
}

void collectDoneTasks()
{
	// 遍历当前保存的 Task 列表，找出已经完成的任务。
	std::vector<PyObject*>::iterator iter = g_tasks.begin();
	while (iter != g_tasks.end())
	{
		PyObject* task = *iter;

		// 通过 task.done() 判断 Task 是否已经结束。
		PyObject* pyDone = PyObject_CallMethod(task, const_cast<char*>("done"), const_cast<char*>(""));
		if (pyDone == NULL)
		{
			SCRIPT_ERROR_CHECK();
			Py_DECREF(task);
			iter = g_tasks.erase(iter);
			continue;
		}

		// 未完成的 Task 继续保留，等待下一次 timer pump。
		bool done = PyObject_IsTrue(pyDone) > 0;
		Py_DECREF(pyDone);

		if (!done)
		{
			++iter;
			continue;
		}

		// 取消的 Task 不读取 result，避免 CancelledError 干扰日志。
		PyObject* pyCancelled = PyObject_CallMethod(task, const_cast<char*>("cancelled"), const_cast<char*>(""));
		if (pyCancelled == NULL)
		{
			SCRIPT_ERROR_CHECK();
		}
		else
		{
			bool cancelled = PyObject_IsTrue(pyCancelled) > 0;
			Py_DECREF(pyCancelled);

			// 非取消任务读取 result，让协程异常进入 KBEngine 脚本错误日志。
			if (!cancelled)
			{
				PyObject* pyRet = PyObject_CallMethod(task, const_cast<char*>("result"), const_cast<char*>(""));
				if (pyRet == NULL)
				{
					SCRIPT_ERROR_CHECK();
				}
				else
				{
					Py_DECREF(pyRet);
				}
			}
		}

		// 完成后的 Task 释放强引用，并从列表移除。
		Py_DECREF(task);
		iter = g_tasks.erase(iter);
	}
}

bool pumpLoopOnce()
{
	// 把 loop.stop 安排到 ready 队列，保证 run_forever 本轮只跑一次就返回。
	PyObject* stopFunc = PyObject_GetAttrString(g_loop, "stop");
	if (stopFunc == NULL)
	{
		SCRIPT_ERROR_CHECK();
		return false;
	}

	PyObject* pyRet = PyObject_CallMethod(g_loop, const_cast<char*>("call_soon"), const_cast<char*>("O"), stopFunc);
	Py_DECREF(stopFunc);

	if (pyRet == NULL)
	{
		SCRIPT_ERROR_CHECK();
		return false;
	}

	// 非阻塞推进 asyncio；只处理当前 ready 的回调，不长期占住主线程。
	Py_DECREF(pyRet);

	pyRet = PyObject_CallMethod(g_loop, const_cast<char*>("run_forever"), const_cast<char*>(""));
	if (pyRet == NULL)
	{
		SCRIPT_ERROR_CHECK();
		return false;
	}

	Py_DECREF(pyRet);
	return true;
}

void pumpLoop()
{
	// 确保主线程 event loop 已经创建。
	if (!ensureLoop())
		return;

	// 把“约2ms”转换成当前平台的timestamp单位，最小为1，避免低精度平台预算为0。
	uint64 maxPumpStamps = stampsPerSecond() * ASYNCIO_MAX_PUMP_STAMPS / 1000;
	if (maxPumpStamps == 0)
		maxPumpStamps = 1;

	uint64 beginStamps = timestamp();

	for (int i = 0; i < ASYNCIO_MAX_PUMP_ITERATIONS; ++i)
	{
		// 每一轮只做一次非阻塞run_forever；多轮组合用于drain aiohttp的多层ready回调。
		if (!pumpLoopOnce())
			return;

		// 每轮后回收已完成任务，并输出异常。
		collectDoneTasks();

		// 至少跑几轮，让accept、request task创建、handler启动这些链式回调能在同一tick内推进。
		if (i + 1 < ASYNCIO_MIN_PUMP_ITERATIONS)
			continue;

		// ready队列为空且已达到最小轮数，就把主循环时间还给KBEngine。
		if (readySize() <= 0)
			break;

		// 如果asyncio本轮已经消耗过多时间，也停止继续drain，避免卡主线程。
		if (timestamp() - beginStamps >= maxPumpStamps)
			break;
	}

	// 结束前再回收一次，确保最后一轮完成的Task不滞留。
	collectDoneTasks();
}

class AsyncioTimerHandler : public TimerHandler
{
public:
	AsyncioTimerHandler()
	{
	}

private:
	virtual void handleTimeout(TimerHandle handle, void* pUser)
	{
		// KBEngine timer 触发时，只在主线程推进一次 asyncio loop。
		pumpLoop();
	}

	virtual void onRelease(TimerHandle handle, void* /*pUser*/)
	{
		// timer被取消时清理全局句柄，并释放handler自身。
		g_timerHandle.clearWithoutCancel();
		delete this;
	}
};
}
	
	PyObject* AsyncioHelper::submitCoroutine(PyObject* pyObject)
	{
		// 空对象直接忽略，保持原接口的宽容行为。
		if (pyObject == NULL)
			return NULL;

		// 只接收实现 __await__ 的 awaitable；普通返回值不处理。
		int isAwaitable = PyObject_HasAttrString(pyObject, "__await__");
		if (isAwaitable <= 0)
			return NULL;

		// 关闭期间拒绝新任务；如果是 coroutine，尝试 close 避免 RuntimeWarning。
		if (g_shuttingDown)
		{
			PyObject* pyRet = PyObject_CallMethod(pyObject, const_cast<char*>("close"), const_cast<char*>(""));
			Py_XDECREF(pyRet);
			return NULL;
		}

		// 确保主线程 event loop 已经可用。
		if (!ensureLoop())
			return NULL;

		// 把 awaitable 包装成 asyncio Task，后续由 timer 驱动执行。
		PyObject* task = PyObject_CallMethod(g_loop, const_cast<char*>("create_task"), const_cast<char*>("O"), pyObject);
		if (task == NULL)
		{
			SCRIPT_ERROR_CHECK();
			return NULL;
		}

		// 保存 Task 强引用；完成后 collectDoneTasks 会释放。
		g_tasks.push_back(task);
		return NULL;
	}

	bool AsyncioHelper::installTimer(Network::EventDispatcher& dispatcher)
	{
		// 配置小于等于 0 表示关闭 asyncio timer。
		if (g_kbeSrvConfig.asyncioRepeatOffset() <= 0.f)
			return true;

		// 如果已经安装过底层timer，直接复用，避免重复pump同一个loop。
		if (g_timerHandle.isSet())
			return true;

		// 安装 timer 前先初始化 loop，初始化失败则阻止 app 继续启动。
		if (!ensureLoop())
			return false;

		// 创建底层EventDispatcher timer handler，用主线程周期性pump asyncio。
		AsyncioTimerHandler* handler = new AsyncioTimerHandler();

		// 按秒配置转成微秒级dispatcher timer，不再受gameUpdateHertz的tick粒度限制。
		int64 intervalUS = KBE_MAX(int64(1000), int64(g_kbeSrvConfig.asyncioRepeatOffset() * 1000000.f));
		g_timerHandle = dispatcher.addTimer(intervalUS, handler);
		if (!g_timerHandle.isSet())
		{
			delete handler;
			ERROR_MSG("AsyncioHelper::installTimer: unable to add asyncio timer.\n");
			return false;
		}

		return true;
	}

	void AsyncioHelper::shutdown()
	{
		// 如果 loop 从未创建，说明没有 async 任务需要清理。
		if (g_loop == NULL)
			return;

		// 进入关闭状态，后续 submitCoroutine 会拒绝新任务。
		g_shuttingDown = true;

		// 先取消底层dispatcher timer，避免关闭过程中继续pump loop。
		if (g_timerHandle.isSet())
			g_timerHandle.cancel();

		// 取消所有未完成 Task，让协程有机会处理取消。
		for (std::vector<PyObject*>::iterator iter = g_tasks.begin(); iter != g_tasks.end(); ++iter)
		{
			PyObject* pyRet = PyObject_CallMethod(*iter, const_cast<char*>("cancel"), const_cast<char*>(""));
			Py_XDECREF(pyRet);
		}

		// 再 pump 一次，让取消回调和 finally 块尽量执行。
		pumpLoop();
		collectDoneTasks();

		// 如果仍有未完成 Task，释放 C++ 强引用，避免关闭时残留引用。
		for (std::vector<PyObject*>::iterator iter = g_tasks.begin(); iter != g_tasks.end(); ++iter)
		{
			Py_DECREF(*iter);
		}
		g_tasks.clear();

		// 关闭 Python event loop。
		PyObject* pyRet = PyObject_CallMethod(g_loop, const_cast<char*>("close"), const_cast<char*>(""));
		if (pyRet == NULL)
		{
			SCRIPT_ERROR_CHECK();
		}
		else
		{
			Py_DECREF(pyRet);
		}

		// 释放模块和 loop 引用，下一次初始化可以重新创建。
		Py_CLEAR(g_loop);
		Py_CLEAR(g_asyncioModule);
		g_shuttingDown = false;
	}
}
