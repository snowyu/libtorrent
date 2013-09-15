/*

Copyright (c) 2009-2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/allocator.hpp"
#include "libtorrent/config.hpp"
#include "libtorrent/assert.hpp" // for print_backtrace

#ifdef TORRENT_WINDOWS
#include <windows.h>
#elif defined TORRENT_BEOS
#include <kernel/OS.h>
#include <stdlib.h> // malloc/free
#else
#include <stdlib.h> // posix_memalign/free
#include <unistd.h> // _SC_PAGESIZE
#endif

#if TORRENT_USE_MEMALIGN || TORRENT_USE_POSIX_MEMALIGN || defined TORRENT_WINDOWS
#include <malloc.h> // memalign and _aligned_malloc
#endif

#ifdef TORRENT_DEBUG_BUFFERS
#include <sys/mman.h>
#include "libtorrent/size_type.hpp"

struct alloc_header
{
	libtorrent::size_type size;
	int magic;
	char stack[3072];
};

#endif

namespace libtorrent
{

	int page_size()
	{
		static int s = 0;
		if (s != 0) return s;

#ifdef TORRENT_WINDOWS
		SYSTEM_INFO si;
		GetSystemInfo(&si);
		s = si.dwPageSize;
#elif defined TORRENT_BEOS
		s = B_PAGE_SIZE;
#else
		s = sysconf(_SC_PAGESIZE);
#endif
		// assume the page size is 4 kiB if we
		// fail to query it
		if (s <= 0) s = 4096;
		return s;
	}

	char* page_aligned_allocator::malloc(size_type bytes)
	{
		TORRENT_ASSERT(bytes > 0);
		// just sanity check (this needs to be pretty high
		// for cases where the cache size is several gigabytes)
		TORRENT_ASSERT(bytes < 0x30000000);

		TORRENT_ASSERT(bytes >= page_size());
		TORRENT_ASSERT(bytes % page_size() == 0);
#ifdef TORRENT_DEBUG_BUFFERS
		int page = page_size();
		int num_pages = (bytes + (page-1)) / page + 2;
		int orig_bytes = bytes;
		bytes = num_pages * page;
#endif

		char* ret;
#if TORRENT_USE_POSIX_MEMALIGN
		if (posix_memalign((void**)&ret, page_size(), bytes) != 0) ret = NULL;
#elif TORRENT_USE_MEMALIGN
		ret = (char*)memalign(page_size(), bytes);
#elif defined TORRENT_WINDOWS
		ret = (char*)_aligned_malloc(bytes, page_size());
#elif defined TORRENT_BEOS
		area_id id = create_area("", &ret, B_ANY_ADDRESS
			, (bytes + page_size() - 1) & (page_size()-1), B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
		if (id < B_OK) return NULL;
		ret = (char*)ret;
#else
		ret = (char*)valloc(bytes);
#endif
		if (ret == NULL) return NULL;

#ifdef TORRENT_DEBUG_BUFFERS
		// make the two surrounding pages non-readable and -writable
		alloc_header* h = (alloc_header*)ret;
		h->size = orig_bytes;
		h->magic = 0x1337;
		print_backtrace(h->stack, sizeof(h->stack));
		mprotect(ret, page, PROT_READ);
		mprotect(ret + (num_pages-1) * page, page, PROT_READ);
//		fprintf(stderr, "malloc: %p head: %p tail: %p size: %d\n", ret + page, ret, ret + page + bytes, int(bytes));

		return ret + page;
#endif

		return ret;
	}

#ifdef TORRENT_DEBUG_BUFFERS
	bool page_aligned_allocator::in_use(char const* block)
	{
		int page = page_size();
		alloc_header* h = (alloc_header*)(block - page);
		return h->magic == 0x1337;
	}
#endif

	void page_aligned_allocator::free(char* block)
	{
		if (block == 0) return;

#ifdef TORRENT_DEBUG_BUFFERS
		int page = page_size();
		// make the two surrounding pages non-readable and -writable
		mprotect(block - page, page, PROT_READ | PROT_WRITE);
		alloc_header* h = (alloc_header*)(block - page);
		int num_pages = (h->size + (page-1)) / page + 2;
		TORRENT_ASSERT(h->magic == 0x1337);
		mprotect(block + (num_pages-2) * page, page, PROT_READ | PROT_WRITE);
//		fprintf(stderr, "free: %p head: %p tail: %p size: %d\n", block, block - page, block + h->size, int(h->size));
		h->magic = 0;

		print_backtrace(h->stack, sizeof(h->stack));
		block -= page;
#endif

#ifdef TORRENT_WINDOWS
		_aligned_free(block);
#elif defined TORRENT_BEOS
		area_id id = area_for(block);
		if (id < B_OK) return;
		delete_area(id);
#else
		::free(block);
#endif
	}


}
