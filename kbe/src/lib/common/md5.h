// Copyright 2008-2018 Yolo Technologies, Inc. All Rights Reserved. https://www.comblockengine.com

#ifndef KBENGINE_MD5_H
#define KBENGINE_MD5_H

#include "openssl/md5.h"
#include <string>
#include <cstddef>

namespace KBEngine
{

/**
 *	openssl md5的封装
 */
class KBE_MD5
{
public:
	KBE_MD5();
	KBE_MD5(const void * data, size_t numBytes);
	~KBE_MD5();

	void append(const void * data, size_t numBytes);
	const unsigned char* getDigest();
	std::string getDigestStr();

	void clear();
	
	void final();

	bool operator==( const KBE_MD5 & other ) const;
	bool operator!=( const KBE_MD5 & other ) const
		{ return !(*this == other); }

	bool operator<( const KBE_MD5 & other ) const;

	static std::string getDigest(const void * data, size_t numBytes);

	bool isFinal() const{ return isFinal_; }

private:
	MD5_CTX state_;
	unsigned char bytes_[16];
	bool isFinal_;
};


}

#endif // KBENGINE_MD5_H
