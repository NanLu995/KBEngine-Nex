// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#include "common.h"
#include "strutil.h"
#include <algorithm>
#include <limits>
#include <algorithm>
#include <utility>
#include <functional>
#include <cctype>
#include "utf8cpp/utf8.h"
#include "memorystream.h"

#include <codecvt>

namespace KBEngine{ 
namespace strutil {

	std::wstring to_wide_string(const std::string& input)
	{
		std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
		return converter.from_bytes(input);
	}

	int bytes2string(unsigned char *src, int srcsize, unsigned char *dst, int dstsize)     
	{     
		if (dst != NULL)  
		{  
			*dst = 0;  
		}  
	      
		if (src == NULL || srcsize <= 0 || dst == NULL || dstsize <= srcsize * 2)  
		{  
			return 0;  
		}  
	      
		const char szTable[] = "0123456789ABCDEF";

		for(int i=0; i<srcsize; ++i)     
		{     
			*dst++ = szTable[src[i] >> 4];     
			*dst++ = szTable[src[i] & 0x0f];   
		}     
 
		*dst = 0;      
		return  srcsize * 2;     
	}

	int string2bytes(unsigned char* src, unsigned char* dst, int dstsize)     
	{  
		if(src == NULL)
			return 0;  

		int iLen = (int)strlen((char *)src);
		if (iLen <= 0 || iLen%2 != 0 || dst == NULL || dstsize < iLen/2)  
		{  
			return 0;  
		}  
	      
		iLen /= 2;  
		str_toupper((char *)src); 
		for (int i=0; i<iLen; ++i)  
		{  
			int iVal = 0;  
			unsigned char *pSrcTemp = src + i*2;  
			sscanf((char *)pSrcTemp, "%02x", &iVal);  
			dst[i] = (unsigned char)iVal;  
		}  
	      
		return iLen;  
	}

    std::string toLower(const std::string& str) {
        std::string t = str;
        std::transform(t.begin(), t.end(), t.begin(), tolower);
        return t;
    }

    std::string toUpper(const std::string& str) {
        std::string t = str;
        std::transform(t.begin(), t.end(), t.begin(), toupper);
        return t;
    }

	std::string &kbe_ltrim(std::string &s) 
	{
		s.erase(s.begin(), std::find_if(s.begin(), s.end(),
			[](unsigned char ch) { return !std::isspace(ch); }));
		return s;
	}

	std::string &kbe_rtrim(std::string &s) 
	{
		s.erase(std::find_if(s.rbegin(), s.rend(),
			[](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
		return s;
	}

	std::string kbe_trim(std::string s)
	{
		return kbe_ltrim(kbe_rtrim(s));
	}

	// 判断是否是空白字符（支持中英文空格）
	inline bool is_space(char32_t ch)
	{
		return ch == U'\u0020'   // 半角空格
			|| ch == U'\u3000'   // 中文全角空格
			|| ch == U'\t'       // Tab
			|| ch == U'\n'       // 换行
			|| ch == U'\r';      // 回车
	}

	// UTF-8 -> UTF-32
	std::u32string utf8_to_u32(const std::string& s)
	{
		std::u32string out;
		size_t i = 0;
		while (i < s.size()) {
			unsigned char c = s[i];
			char32_t cp = 0;
			size_t extra = 0;

			if (c < 0x80) {
				cp = c;
			}
			else if ((c >> 5) == 0x6) {
				cp = c & 0x1F;
				extra = 1;
			}
			else if ((c >> 4) == 0xE) {
				cp = c & 0x0F;
				extra = 2;
			}
			else if ((c >> 3) == 0x1E) {
				cp = c & 0x07;
				extra = 3;
			}
			i++;
			for (size_t j = 0; j < extra && i < s.size(); j++, i++) {
				cp = (cp << 6) | (s[i] & 0x3F);
			}
			out.push_back(cp);
		}
		return out;
	}

	// UTF-32 -> UTF-8
	std::string u32_to_utf8(const std::u32string& s)
	{
		std::string out;
		for (char32_t cp : s) {
			if (cp < 0x80) {
				out.push_back(static_cast<char>(cp));
			}
			else if (cp < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else if (cp < 0x10000) {
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else {
				out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		return out;
	}

	// trim 中文支持
	std::string kbe_unicodeTrim(const std::string& s)
	{
		std::u32string u32 = utf8_to_u32(s);

		size_t start = 0;
		while (start < u32.size() && is_space(u32[start])) {
			start++;
		}

		size_t end = u32.size();
		while (end > start && is_space(u32[end - 1])) {
			end--;
		}

		std::u32string result = u32.substr(start, end - start);
		return u32_to_utf8(result);
	}



	int kbe_replace(std::string& str,  const std::string& pattern,  const std::string& newpat) 
	{ 
		int count = 0; 
		const size_t nsize = newpat.size(); 
		const size_t psize = pattern.size(); 

		for(size_t pos = str.find(pattern, 0);  
			pos != std::string::npos; 
			pos = str.find(pattern,pos + nsize)) 
		{ 
			str.replace(pos, psize, newpat); 
			count++; 
		} 

		return count; 
	}

	int kbe_replace(std::wstring& str,  const std::wstring& pattern,  const std::wstring& newpat) 
	{ 
		int count = 0; 
		const size_t nsize = newpat.size(); 
		const size_t psize = pattern.size(); 

		for(size_t pos = str.find(pattern, 0);  
			pos != std::wstring::npos; 
			pos = str.find(pattern,pos + nsize)) 
		{ 
			str.replace(pos, psize, newpat); 
			count++; 
		} 

		return count; 
	}


	int kbe_splits(const std::string& s, const std::string& delim, std::vector< std::string >& out_result, const bool keep_empty)
	{
		if (delim.empty()) {
			out_result.push_back(s);
			return static_cast<int>(out_result.size());
		}

		std::string::const_iterator substart = s.begin(), subend;

		while (true) {
			subend = std::search(substart, s.end(), delim.begin(), delim.end());
			std::string temp(substart, subend);
			if (keep_empty || !temp.empty()) {
				out_result.push_back(temp);
			}
			if (subend == s.end()) {
				break;
			}
			substart = subend + delim.size();
		}

		return static_cast<int>(out_result.size());
	}

	char* wchar2char(const wchar_t* ts, size_t* outlen)
	{
		int len = (int)((wcslen(ts) + 1) * sizeof(wchar_t));
		char* ccattr =(char *)malloc(len);
		memset(ccattr, 0, len);

		size_t slen = wcstombs(ccattr, ts, len);

		if (outlen)
		{
			if ((size_t)-1 != slen)
				*outlen = slen;
			else
				*outlen = 0;
		}

		return ccattr;
	};

	void wchar2char(const wchar_t* ts, MemoryStream* pOutStream)
	{
		int len = (int)((wcslen(ts) + 1) * sizeof(wchar_t));
		pOutStream->data_resize(pOutStream->wpos() + len);
		size_t slen = wcstombs((char*)&pOutStream->data()[pOutStream->wpos()], ts, len);
		
		if((size_t)-1 != slen)
		{
			pOutStream->wpos(pOutStream->wpos() + slen + 1);
			pOutStream->data()[pOutStream->wpos() - 1] = 0;
		}
	};

	// wchar_t* char2wchar(const char* cs, size_t* outlen)
	// {
	// 	int len = (int)((strlen(cs) + 1) * sizeof(wchar_t));
	// 	wchar_t* ccattr =(wchar_t *)malloc(len);
	// 	memset(ccattr, 0, len);

	// 	size_t slen = mbstowcs(ccattr, cs, len);

	// 	if (outlen)
	// 	{
	// 		if ((size_t)-1 != slen)
	// 			*outlen = slen;
	// 		else
	// 			*outlen = 0;
	// 	}
		
	// 	return ccattr;
	// };



	// 修复g++ 13 下，溢出的问题
	// 目前测试下来Ubuntu 24会报 *** buffer overflow detected ***: terminated
	wchar_t* char2wchar(const char* cs, size_t* outlen)
	{
		if (!cs) return nullptr;

		// 字符串长度（字节）
		size_t csl = strlen(cs);

		// wchar_t 个数 = 字符数 + 1（\0）
		size_t wlen = csl + 1;

		// 分配空间：按 wchar_t 数组大小
		wchar_t* ccattr = (wchar_t*)malloc(wlen * sizeof(wchar_t));
		if (!ccattr) {
			if (outlen) *outlen = 0;
			return nullptr;
		}
		memset(ccattr, 0, wlen * sizeof(wchar_t));

		// mbstowcs 的第三个参数是 wchar_t 个数（不是字节数）
		size_t slen = mbstowcs(ccattr, cs, wlen);

		if (outlen) {
			if (slen != (size_t)-1)
				*outlen = slen;
			else
				*outlen = 0;
		}

		return ccattr;
	}


	/*
	int wchar2utf8(const wchar_t* in, int in_len, char* out, int out_max)   
	{   
	#if KBE_PLATFORM == PLATFORM_WIN32   
		BOOL use_def_char;   
		use_def_char = FALSE;   
		return ::WideCharToMultiByte(CP_UTF8, 0, in,in_len / sizeof(wchar_t), out, out_max, NULL, NULL);   
	#else   
		size_t result;   
		iconv_t env;   
	   
		env = iconv_open("UTF8", "WCHAR_T");   
		result = iconv(env,(char**)&in,(size_t*)&in_len,(char**)&out,(size_t*)&out_max);        
		iconv_close(env);   
		return (int) result;   
	#endif   
	}   
	   
	int wchar2utf8(const std::wstring& in, std::string& out)   
	{   
		int len = in.length() + 1;   
		int result;   

		char* pBuffer = new char[len * 4];   

		memset(pBuffer,0,len * 4);               

		result = wchar2utf8(in.c_str(), in.length() * sizeof(wchar_t), pBuffer,len * 4);   

		if(result >= 0)   
		{   
			out = pBuffer;   
		}   
		else   
		{   
			out = "";   
		}   

		delete[] pBuffer;   
		return result;   
	}   
	   
	int utf82wchar(const char* in, int in_len, wchar_t* out, int out_max)   
	{   
	#if KBE_PLATFORM == PLATFORM_WIN32   
		return ::MultiByteToWideChar(CP_UTF8, 0, in, in_len, out, out_max);   
	#else   
		size_t result;   
		iconv_t env;   
		env = iconv_open("WCHAR_T", "UTF8");   
		result = iconv(env,(char**)&in, (size_t*)&in_len, (char**)&out,(size_t*)&out_max);   
		iconv_close(env);   
		return (int) result;   
	#endif   
	}   
	   
	int utf82wchar(const std::string& in, std::wstring& out)   
	{   
		int len = in.length() + 1;   
		int result;   
	 
		wchar_t* pBuffer = new wchar_t[len];   
		memset(pBuffer,0,len * sizeof(wchar_t));   
		result = utf82wchar(in.c_str(), in.length(), pBuffer, len*sizeof(wchar_t));   

		if(result >= 0)   
		{   
			out = pBuffer;   
		}   
		else   
		{   
			out.clear();         
		}   

		delete[] pBuffer;   
		return result;   
	}   
	*/

	size_t utf8length(std::string& utf8str)
	{
		try
		{
			return utf8::distance(utf8str.c_str(), 
				utf8str.c_str() + utf8str.size());
		}
		catch (std::exception&)
		{
			utf8str = "";
			return 0;
		}
	}

	void utf8truncate(std::string& utf8str, size_t len)
	{
		try
		{
			size_t wlen = utf8::distance(utf8str.c_str(), 
				utf8str.c_str() + utf8str.size());
			if (wlen <= len)
				return;

			std::wstring wstr;
			wstr.resize(wlen);
			utf8::utf8to16(utf8str.c_str(), utf8str.c_str() + 
				utf8str.size(), &wstr[0]);
			wstr.resize(len);

			char* oend = utf8::utf16to8(wstr.c_str(), 
				wstr.c_str() + wstr.size(), &utf8str[0]);

			utf8str.resize(oend - (&utf8str[0]));
		}
		catch (std::exception&)
		{
			utf8str = "";
		}
	}

	bool utf82wchar(char const* utf8str, size_t csize, 
		wchar_t* wstr, size_t& wsize)
	{
		try
		{
			size_t len = utf8::distance(utf8str, utf8str + csize);

			if (len > wsize)
			{
				if (wsize > 0)
					wstr[0] = L'\0';
				wsize = 0;
				return false;
			}

			wsize = len;
			utf8::utf8to16(utf8str, utf8str + csize, wstr);
			wstr[len] = L'\0';
		}
		catch (std::exception&)
		{
			if (wsize > 0)
				wstr[0] = L'\0';
			wsize = 0;
			return false;
		}

		return true;
	}

	bool utf82wchar(const std::string& utf8str, std::wstring& wstr)
	{
		try
		{
			size_t len = utf8::distance(utf8str.c_str(), 
				utf8str.c_str() + utf8str.size());
			wstr.resize(len);

			if (len)
				utf8::utf8to16(utf8str.c_str(), 
				utf8str.c_str() + utf8str.size(), &wstr[0]);
		}
		catch (std::exception&)
		{
			wstr = L"";
			return false;
		}

		return true;
	}

	bool wchar2utf8(const wchar_t* wstr, size_t size, std::string& utf8str)
	{
		try
		{
			std::string utf8str2;
			utf8str2.resize(size * 4);                          // allocate for most long case

			char* oend = utf8::utf16to8(wstr, wstr + size, &utf8str2[0]);
			utf8str2.resize(oend - (&utf8str2[0]));             // remove unused tail
			utf8str = utf8str2;
		}
		catch (std::exception&)
		{
			utf8str = "";
			return false;
		}

		return true;
	}

	bool wchar2utf8(const std::wstring& wstr, std::string& utf8str)
	{
		try
		{
			std::string utf8str2;
			utf8str2.resize(wstr.size() * 4);                   // allocate for most long case

			char* oend = utf8::utf16to8(wstr.c_str(), 
				wstr.c_str() + wstr.size(), &utf8str2[0]);

			utf8str2.resize(oend - (&utf8str2[0]));             // remove unused tail
			utf8str = utf8str2;
		}
		catch (std::exception&)
		{
			utf8str = "";
			return false;
		}

		return true;
	}
}

}
