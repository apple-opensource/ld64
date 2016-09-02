/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009-2011 Apple Inc. All rights reserved.
 *
 * @APPLE_LICENSE_HEADER_START@
 *
 * This file contains Original Code and/or Modifications of Original Code
 * as defined in and that are subject to the Apple Public Source License
 * Version 2.0 (the 'License'). You may not use this file except in
 * compliance with the License. Please obtain a copy of the License at
 * http://www.opensource.apple.com/apsl/ and read it before using this
 * file.
 *
 * The Original Code and all software distributed under the License are
 * distributed on an 'AS IS' basis, WITHOUT WARRANTY OF ANY KIND, EITHER
 * EXPRESS OR IMPLIED, AND APPLE HEREBY DISCLAIMS ALL SUCH WARRANTIES,
 * INCLUDING WITHOUT LIMITATION, ANY WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE, QUIET ENJOYMENT OR NON-INFRINGEMENT.
 * Please see the License for the specific language governing rights and
 * limitations under the License.
 *
 * @APPLE_LICENSE_HEADER_END@
 */

#ifndef __INPUT_FILES_H__
#define __INPUT_FILES_H__

#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <mach/mach_time.h>
#include <mach/vm_statistics.h>
#include <mach/mach_init.h>
#include <mach/mach_host.h>
#include <dlfcn.h>
#include <mach-o/dyld.h>

#include <vector>

#include "Options.h"
#include "ld.hpp"

namespace ld {
namespace tool {

class InputFiles : public ld::dylib::File::DylibHandler
{
public:
								InputFiles(Options& opts, const char** archName);

	// implementation from ld::dylib::File::DylibHandler
	virtual ld::dylib::File*	findDylib(const char* installPath, const char* fromPath);
	
	// iterates all atoms in initial files
	bool						forEachInitialAtom(ld::File::AtomHandler&) const;
	// searches libraries for name
	bool						searchLibraries(const char* name, bool searchDylibs, bool searchArchives,  
																  bool dataSymbolOnly, ld::File::AtomHandler&) const;
	// see if any linked dylibs export a weak def of symbol
	bool						searchWeakDefInDylib(const char* name) const;
	// copy dylibs to link with in command line order
	void						dylibs(ld::Internal& state);
	
	bool						inferredArch() const { return _inferredArch; }
	
	uint32_t					nextInputOrdinal() const  { return _nextInputOrdinal++; }
	
	// for -print_statistics
	uint64_t					_totalObjectSize;
	uint64_t					_totalArchiveSize;
	uint32_t					_totalObjectLoaded;
	uint32_t					_totalArchivesLoaded;
	uint32_t					_totalDylibsLoaded;
	
	
private:
	void						inferArchitecture(Options& opts, const char** archName);
	const char*					fileArch(const uint8_t* p, unsigned len);
	ld::File*					makeFile(const Options::FileInfo& info, bool indirectDylib);
	ld::File*					addDylib(ld::dylib::File* f,        const Options::FileInfo& info, uint64_t mappedLen);
	ld::File*					addObject(ld::relocatable::File* f, const Options::FileInfo& info, uint64_t mappedLen);
	ld::File*					addArchive(ld::File* f,             const Options::FileInfo& info, uint64_t mappedLen);
	void						logTraceInfo (const char* format, ...) const;
	void						logDylib(ld::File*, bool indirect);
	void						logArchive(ld::File*) const;
	void						createIndirectDylibs();
	void						checkDylibClientRestrictions(ld::dylib::File*);
	void						createOpaqueFileSections();

	class CStringEquals {
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};
	typedef __gnu_cxx::hash_map<const char*, ld::dylib::File*, __gnu_cxx::hash<const char*>, CStringEquals>	InstallNameToDylib;

	const Options&				_options;
	std::vector<ld::File*>		_inputFiles;
	mutable std::set<class ld::File*>	_archiveFilesLogged;
	InstallNameToDylib			_installPathToDylibs;
	std::set<ld::dylib::File*>	_allDylibs;
	ld::dylib::File*			_bundleLoader;
	mutable uint32_t			_nextInputOrdinal;
	bool						_allDirectDylibsLoaded;
	bool						_inferredArch;
};

} // namespace tool 
} // namespace ld 

#endif // __INPUT_FILES_H__
