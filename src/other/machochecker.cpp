/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*- 
 *
 * Copyright (c) 2006-2010 Apple Inc. All rights reserved.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <stdarg.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <vector>
#include <set>
#include <ext/hash_set>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"


 __attribute__((noreturn))
void throwf(const char* format, ...) 
{
	va_list	list;
	char*	p;
	va_start(list, format);
	vasprintf(&p, format, list);
	va_end(list);
	
	const char*	t = p;
	throw t;
}

static uint64_t read_uleb128(const uint8_t*& p, const uint8_t* end)
{
	uint64_t result = 0;
	int		 bit = 0;
	do {
		if (p == end)
			throwf("malformed uleb128");

		uint64_t slice = *p & 0x7f;

		if (bit >= 64 || slice << bit >> bit != slice)
			throwf("uleb128 too big");
		else {
			result |= (slice << bit);
			bit += 7;
		}
	} 
	while (*p++ & 0x80);
	return result;
}


static int64_t read_sleb128(const uint8_t*& p, const uint8_t* end)
{
	int64_t result = 0;
	int bit = 0;
	uint8_t byte;
	do {
		if (p == end)
			throwf("malformed sleb128");
		byte = *p++;
		result |= ((byte & 0x7f) << bit);
		bit += 7;
	} while (byte & 0x80);
	// sign extend negative numbers
	if ( (byte & 0x40) != 0 )
		result |= (-1LL) << bit;
	return result;
}


template <typename A>
class MachOChecker
{
public:
	static bool									validFile(const uint8_t* fileContent);
	static MachOChecker<A>*						make(const uint8_t* fileContent, uint32_t fileLength, const char* path) 
														{ return new MachOChecker<A>(fileContent, fileLength, path); }
	virtual										~MachOChecker() {}


private:
	typedef typename A::P					P;
	typedef typename A::P::E				E;
	typedef typename A::P::uint_t			pint_t;
	
	class CStringEquals
	{
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};

	typedef __gnu_cxx::hash_set<const char*, __gnu_cxx::hash<const char*>, CStringEquals>  StringSet;

												MachOChecker(const uint8_t* fileContent, uint32_t fileLength, const char* path);
	void										checkMachHeader();
	void										checkLoadCommands();
	void										checkSection(const macho_segment_command<P>* segCmd, const macho_section<P>* sect);
	uint8_t										loadCommandSizeMask();
	void										checkSymbolTable();
	void										checkInitTerms();
	void										checkIndirectSymbolTable();
	void										checkRelocations();
	void										checkExternalReloation(const macho_relocation_info<P>* reloc);
	void										checkLocalReloation(const macho_relocation_info<P>* reloc);
	pint_t										relocBase();
	bool										addressInWritableSegment(pint_t address);
	bool										hasTextRelocInRange(pint_t start, pint_t end);
	pint_t										segStartAddress(uint8_t segIndex);
	bool										addressIsRebaseSite(pint_t addr);
	bool										addressIsBindingSite(pint_t addr);
	pint_t										getInitialStackPointer(const macho_thread_command<P>*);
	pint_t										getEntryPoint(const macho_thread_command<P>*);
	
	
	
	const char*									fPath;
	const macho_header<P>*						fHeader;
	uint32_t									fLength;
	const char*									fStrings;
	const char*									fStringsEnd;
	const macho_nlist<P>*						fSymbols;
	uint32_t									fSymbolCount;
	const macho_dysymtab_command<P>*			fDynamicSymbolTable;
	const uint32_t*								fIndirectTable;
	uint32_t									fIndirectTableCount;
	const macho_relocation_info<P>*				fLocalRelocations;
	uint32_t									fLocalRelocationsCount;
	const macho_relocation_info<P>*				fExternalRelocations;
	uint32_t									fExternalRelocationsCount;
	bool										fWriteableSegmentWithAddrOver4G;
	bool										fSlidableImage;
	const macho_segment_command<P>*				fFirstSegment;
	const macho_segment_command<P>*				fFirstWritableSegment;
	const macho_segment_command<P>*				fTEXTSegment;
	const macho_dyld_info_command<P>*			fDyldInfo;
	uint32_t									fSectionCount;
	std::vector<const macho_segment_command<P>*>fSegments;
};



template <>
bool MachOChecker<ppc>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <>
bool MachOChecker<ppc64>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_POWERPC64 )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <>
bool MachOChecker<x86>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_I386 )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <>
bool MachOChecker<x86_64>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC_64 )
		return false;
	if ( header->cputype() != CPU_TYPE_X86_64 )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <>
bool MachOChecker<arm>::validFile(const uint8_t* fileContent)
{	
	const macho_header<P>* header = (const macho_header<P>*)fileContent;
	if ( header->magic() != MH_MAGIC )
		return false;
	if ( header->cputype() != CPU_TYPE_ARM )
		return false;
	switch (header->filetype()) {
		case MH_EXECUTE:
		case MH_DYLIB:
		case MH_BUNDLE:
		case MH_DYLINKER:
			return true;
	}
	return false;
}

template <> uint8_t MachOChecker<ppc>::loadCommandSizeMask()	{ return 0x03; }
template <> uint8_t MachOChecker<ppc64>::loadCommandSizeMask()	{ return 0x07; }
template <> uint8_t MachOChecker<x86>::loadCommandSizeMask()	{ return 0x03; }
template <> uint8_t MachOChecker<x86_64>::loadCommandSizeMask() { return 0x07; }
template <> uint8_t MachOChecker<arm>::loadCommandSizeMask()	{ return 0x03; }


template <>
ppc::P::uint_t MachOChecker<ppc>::getInitialStackPointer(const macho_thread_command<ppc::P>* threadInfo)
{
	return threadInfo->thread_register(3);
}

template <>
ppc64::P::uint_t MachOChecker<ppc64>::getInitialStackPointer(const macho_thread_command<ppc64::P>* threadInfo)
{
	return threadInfo->thread_register(3);
}

template <>
x86::P::uint_t MachOChecker<x86>::getInitialStackPointer(const macho_thread_command<x86::P>* threadInfo)
{
	return threadInfo->thread_register(7);
}

template <>
x86_64::P::uint_t MachOChecker<x86_64>::getInitialStackPointer(const macho_thread_command<x86_64::P>* threadInfo)
{
	return threadInfo->thread_register(7);
}

template <>
arm::P::uint_t MachOChecker<arm>::getInitialStackPointer(const macho_thread_command<arm::P>* threadInfo)
{
	return threadInfo->thread_register(13);
}





template <>
ppc::P::uint_t MachOChecker<ppc>::getEntryPoint(const macho_thread_command<ppc::P>* threadInfo)
{
	return threadInfo->thread_register(0);
}

template <>
ppc64::P::uint_t MachOChecker<ppc64>::getEntryPoint(const macho_thread_command<ppc64::P>* threadInfo)
{
	return threadInfo->thread_register(0);
}

template <>
x86::P::uint_t MachOChecker<x86>::getEntryPoint(const macho_thread_command<x86::P>* threadInfo)
{
	return threadInfo->thread_register(10);
}

template <>
x86_64::P::uint_t MachOChecker<x86_64>::getEntryPoint(const macho_thread_command<x86_64::P>* threadInfo)
{
	return threadInfo->thread_register(16);
}

template <>
arm::P::uint_t MachOChecker<arm>::getEntryPoint(const macho_thread_command<arm::P>* threadInfo)
{
	return threadInfo->thread_register(15);
}


template <typename A>
MachOChecker<A>::MachOChecker(const uint8_t* fileContent, uint32_t fileLength, const char* path)
 : fHeader(NULL), fLength(fileLength), fStrings(NULL), fSymbols(NULL), fSymbolCount(0), fDynamicSymbolTable(NULL), fIndirectTableCount(0),
 fLocalRelocations(NULL),  fLocalRelocationsCount(0),  fExternalRelocations(NULL),  fExternalRelocationsCount(0),
 fWriteableSegmentWithAddrOver4G(false), fSlidableImage(false), fFirstSegment(NULL), fFirstWritableSegment(NULL), 
 fTEXTSegment(NULL), fDyldInfo(NULL), fSectionCount(0)
{
	// sanity check
	if ( ! validFile(fileContent) )
		throw "not a mach-o file that can be checked";

	fPath = strdup(path);
	fHeader = (const macho_header<P>*)fileContent;
	
	// sanity check header
	checkMachHeader();
	
	// check load commands
	checkLoadCommands();
	
	checkIndirectSymbolTable();

	checkRelocations();
	
	checkSymbolTable();
	
	checkInitTerms();
}


template <typename A>
void MachOChecker<A>::checkMachHeader()
{
	if ( (fHeader->sizeofcmds() + sizeof(macho_header<P>)) > fLength )
		throw "sizeofcmds in mach_header is larger than file";
	
	uint32_t flags = fHeader->flags();
	const uint32_t invalidBits = MH_INCRLINK | MH_LAZY_INIT | 0xFE000000;
	if ( flags & invalidBits )
		throw "invalid bits in mach_header flags";
	if ( (flags & MH_NO_REEXPORTED_DYLIBS) && (fHeader->filetype() != MH_DYLIB) ) 
		throw "MH_NO_REEXPORTED_DYLIBS bit of mach_header flags only valid for dylibs";
	
	switch ( fHeader->filetype() ) {
		case MH_EXECUTE:
			fSlidableImage = ( flags & MH_PIE );
			break;
		case MH_DYLIB:
		case MH_BUNDLE:
			fSlidableImage = true;
			break;
		default:
			throw "not a mach-o file type supported by this tool";
	}
}

template <typename A>
void MachOChecker<A>::checkLoadCommands()
{
	// check that all load commands fit within the load command space file
	const macho_encryption_info_command<P>* encryption_info = NULL;
	const macho_thread_command<P>* threadInfo = NULL;
	const uint8_t* const endOfFile = (uint8_t*)fHeader + fLength;
	const uint8_t* const endOfLoadCommands = (uint8_t*)fHeader + sizeof(macho_header<P>) + fHeader->sizeofcmds();
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		uint32_t size = cmd->cmdsize();
		if ( (size & this->loadCommandSizeMask()) != 0 )
			throwf("load command #%d has a unaligned size", i);
		const uint8_t* endOfCmd = ((uint8_t*)cmd)+cmd->cmdsize();
		if ( endOfCmd > endOfLoadCommands )
			throwf("load command #%d extends beyond the end of the load commands", i);
		if ( endOfCmd > endOfFile )
			throwf("load command #%d extends beyond the end of the file", i);
		switch ( cmd->cmd()	) {
			case macho_segment_command<P>::CMD:
			case LC_SYMTAB:
			case LC_DYSYMTAB:
			case LC_LOAD_DYLIB:
			case LC_ID_DYLIB:
			case LC_LOAD_DYLINKER:
			case LC_ID_DYLINKER:
			case macho_routines_command<P>::CMD:
			case LC_SUB_FRAMEWORK:
			case LC_SUB_CLIENT:
			case LC_TWOLEVEL_HINTS:
			case LC_PREBIND_CKSUM:
			case LC_LOAD_WEAK_DYLIB:
			case LC_LAZY_LOAD_DYLIB:
			case LC_UUID:
			case LC_REEXPORT_DYLIB:
			case LC_SEGMENT_SPLIT_INFO:
			case LC_CODE_SIGNATURE:
			case LC_LOAD_UPWARD_DYLIB:
			case LC_VERSION_MIN_MACOSX:
			case LC_VERSION_MIN_IPHONEOS:
			case LC_FUNCTION_STARTS:
			case LC_RPATH:
				break;
			case LC_DYLD_INFO:
			case LC_DYLD_INFO_ONLY:
				fDyldInfo = (macho_dyld_info_command<P>*)cmd;
				break;
			case LC_ENCRYPTION_INFO:
				encryption_info = (macho_encryption_info_command<P>*)cmd;
				break;
			case LC_SUB_UMBRELLA:
			case LC_SUB_LIBRARY:
				if ( fHeader->flags() & MH_NO_REEXPORTED_DYLIBS )
					throw "MH_NO_REEXPORTED_DYLIBS bit of mach_header flags should not be set in an image with LC_SUB_LIBRARY or LC_SUB_UMBRELLA";
				break;
			case LC_UNIXTHREAD:
				if ( fHeader->filetype() != MH_EXECUTE )
					throw "LC_UNIXTHREAD can only be used in MH_EXECUTE file types";
				threadInfo = (macho_thread_command<P>*)cmd;
				break;
			default:
				throwf("load command #%d is an unknown kind 0x%X", i, cmd->cmd());
		}
		cmd = (const macho_load_command<P>*)endOfCmd;
	}
	
	// check segments
	cmd = cmds;
	std::vector<std::pair<pint_t, pint_t> > segmentAddressRanges;
	std::vector<std::pair<pint_t, pint_t> > segmentFileOffsetRanges;
	const macho_segment_command<P>* linkEditSegment = NULL;
	const macho_segment_command<P>* stackSegment = NULL;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			fSegments.push_back(segCmd);
			if ( segCmd->cmdsize() != (sizeof(macho_segment_command<P>) + segCmd->nsects() * sizeof(macho_section_content<P>)) )
				throw "invalid segment load command size";
				
			// see if this overlaps another segment address range
			uint64_t startAddr = segCmd->vmaddr();
			uint64_t endAddr = startAddr + segCmd->vmsize();
			for (typename std::vector<std::pair<pint_t, pint_t> >::iterator it = segmentAddressRanges.begin(); it != segmentAddressRanges.end(); ++it) {
				if ( it->first < startAddr ) {
					if ( it->second > startAddr )
						throw "overlapping segment vm addresses";
				}
				else if ( it->first > startAddr ) {
					if ( it->first < endAddr )
						throw "overlapping segment vm addresses";
				}
				else {
					throw "overlapping segment vm addresses";
				}
				segmentAddressRanges.push_back(std::make_pair<pint_t, pint_t>(startAddr, endAddr));
			}
			// see if this overlaps another segment file offset range
			uint64_t startOffset = segCmd->fileoff();
			uint64_t endOffset = startOffset + segCmd->filesize();
			for (typename std::vector<std::pair<pint_t, pint_t> >::iterator it = segmentFileOffsetRanges.begin(); it != segmentFileOffsetRanges.end(); ++it) {
				if ( it->first < startOffset ) {
					if ( it->second > startOffset )
						throw "overlapping segment file data";
				}
				else if ( it->first > startOffset ) {
					if ( it->first < endOffset )
						throw "overlapping segment file data";
				}
				else {
					throw "overlapping segment file data";
				}
				segmentFileOffsetRanges.push_back(std::make_pair<pint_t, pint_t>(startOffset, endOffset));
				// check is within file bounds
				if ( (startOffset > fLength) || (endOffset > fLength) )
					throw "segment file data is past end of file";
			}
			// verify it fits in file
			if ( startOffset > fLength )
				throw "segment fileoff does not fit in file";
			if ( endOffset > fLength )
				throw "segment fileoff+filesize does not fit in file";
				
			// record special segments
			if ( strcmp(segCmd->segname(), "__LINKEDIT") == 0 )
				linkEditSegment = segCmd;
			else if ( strcmp(segCmd->segname(), "__UNIXSTACK") == 0 )
				stackSegment = segCmd;			

			// cache interesting segments
			if ( fFirstSegment == NULL )
				fFirstSegment = segCmd;
			if ( (fTEXTSegment == NULL) && (strcmp(segCmd->segname(), "__TEXT") == 0) )
				fTEXTSegment = segCmd;
			if ( (segCmd->initprot() & VM_PROT_WRITE) != 0 ) {
				if ( fFirstWritableSegment == NULL )
					fFirstWritableSegment = segCmd;
				if ( segCmd->vmaddr() > 0x100000000ULL )
					fWriteableSegmentWithAddrOver4G = true;
			}
				
			// check section ranges
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
			for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
				// check all non-zero sized sections are within segment
				if ( sect->addr() < startAddr )
					throwf("section %s vm address not within segment", sect->sectname());
				if ( (sect->addr()+sect->size()) > endAddr )
					throwf("section %s vm address not within segment", sect->sectname());
				if ( ((sect->flags() & SECTION_TYPE) != S_ZEROFILL) 
					&& ((sect->flags() & SECTION_TYPE) != S_THREAD_LOCAL_ZEROFILL) 
					&& (segCmd->filesize() != 0) 
					&& (sect->size() != 0) ) {
					if ( sect->offset() < startOffset )
						throwf("section %s file offset not within segment", sect->sectname());
					if ( (sect->offset()+sect->size()) > endOffset )
						throwf("section %s file offset not within segment", sect->sectname());
				}	
				checkSection(segCmd, sect);
				++fSectionCount;
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	
	// verify there was a LINKEDIT segment
	if ( linkEditSegment == NULL )
		throw "no __LINKEDIT segment";
	
	// verify there was an executable __TEXT segment and load commands are in it
	if ( fTEXTSegment == NULL )
		throw "no __TEXT segment";
	if ( fTEXTSegment->initprot() != (VM_PROT_READ|VM_PROT_EXECUTE) )
		throw "__TEXT segment does not have r-x init permissions";
	//if ( fTEXTSegment->maxprot() != (VM_PROT_READ|VM_PROT_EXECUTE|VM_PROT_WRITE) )
	//	throw "__TEXT segment does not have rwx max permissions";
	if ( fTEXTSegment->fileoff() != 0 )
		throw "__TEXT segment does not start at mach_header";
	if ( fTEXTSegment->filesize() < (sizeof(macho_header<P>)+fHeader->sizeofcmds()) )
		throw "__TEXT segment smaller than load commands";
	
	// verify if custom stack used, that stack is in __UNIXSTACK segment
	if ( threadInfo != NULL ) {
		pint_t initialSP = getInitialStackPointer(threadInfo);
		if ( initialSP != 0 ) {
			if ( stackSegment == NULL )
				throw "LC_UNIXTHREAD specifics custom initial stack pointer, but no __UNIXSTACK segment";
			if ( (initialSP < stackSegment->vmaddr()) || (initialSP > (stackSegment->vmaddr()+stackSegment->vmsize())) )
				throw "LC_UNIXTHREAD specifics custom initial stack pointer which does not point into __UNIXSTACK segment";
		}
	}
	
	// verify __UNIXSTACK is zero fill 
	if ( stackSegment != NULL ) {
		 if ( (stackSegment->filesize() != 0) || (stackSegment->fileoff() != 0) )
			throw "__UNIXSTACK is not a zero-fill segment";
		 if ( stackSegment->vmsize() < 4096 )
			throw "__UNIXSTACK segment is too small";
	}
	
	// verify entry point is in __TEXT segment 
	if ( threadInfo != NULL ) {
		pint_t initialPC = getEntryPoint(threadInfo);
		if ( (initialPC < fTEXTSegment->vmaddr()) ||  (initialPC >= (fTEXTSegment->vmaddr()+fTEXTSegment->vmsize())) ) 
			throwf("entry point 0x%0llX is outside __TEXT segment", (long long)initialPC);
	}
	
	
	// checks for executables
	bool isStaticExecutable = false;
	if ( fHeader->filetype() == MH_EXECUTE ) {
		isStaticExecutable = true;
		cmd = cmds;
		for (uint32_t i = 0; i < cmd_count; ++i) {
			switch ( cmd->cmd() ) {
				case LC_LOAD_DYLINKER:
					// the existence of a dyld load command makes a executable dynamic
					isStaticExecutable = false;
					break;
			}
			cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
		}
		if ( isStaticExecutable ) {
			if ( fHeader->flags() != MH_NOUNDEFS )
				throw "invalid bits in mach_header flags for static executable";
		}
	}

	// verify encryption info
	if ( encryption_info != NULL ) {
		if ( fHeader->filetype() != MH_EXECUTE )
			throw "LC_ENCRYPTION_INFO load command is only legal in main executables";
		if ( encryption_info->cryptoff() <  (sizeof(macho_header<P>) + fHeader->sizeofcmds()) )
			throw "LC_ENCRYPTION_INFO load command has cryptoff covers some load commands";
		if ( (encryption_info->cryptoff() % 4096) != 0 )
			throw "LC_ENCRYPTION_INFO load command has cryptoff which is not page aligned";
		if ( (encryption_info->cryptsize() % 4096) != 0 )
			throw "LC_ENCRYPTION_INFO load command has cryptsize which is not page sized";
		for (typename std::vector<std::pair<pint_t, pint_t> >::iterator it = segmentFileOffsetRanges.begin(); 
																it != segmentFileOffsetRanges.end(); ++it) {
			if ( (it->first <= encryption_info->cryptoff()) && (encryption_info->cryptoff() < it->second) ) {
				if ( (encryption_info->cryptoff() + encryption_info->cryptsize()) > it->second )
					throw "LC_ENCRYPTION_INFO load command is not contained within one segment";
			}
		}
	}

	// check LC_SYMTAB, LC_DYSYMTAB, and LC_SEGMENT_SPLIT_INFO
	cmd = cmds;
	bool foundDynamicSymTab = false;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		switch ( cmd->cmd() ) {
			case LC_SYMTAB:
				{
					const macho_symtab_command<P>* symtab = (macho_symtab_command<P>*)cmd;
					fSymbolCount = symtab->nsyms();
					fSymbols = (const macho_nlist<P>*)((char*)fHeader + symtab->symoff());
					if ( symtab->symoff() < linkEditSegment->fileoff() )
						throw "symbol table not in __LINKEDIT";
					if ( (symtab->symoff() + fSymbolCount*sizeof(macho_nlist<P>*)) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "symbol table end not in __LINKEDIT";
					if ( (symtab->symoff() % sizeof(pint_t)) != 0 )
						throw "symbol table start not pointer aligned";
					fStrings = (char*)fHeader + symtab->stroff();
					fStringsEnd = fStrings + symtab->strsize();
					if ( symtab->stroff() < linkEditSegment->fileoff() )
						throw "string pool not in __LINKEDIT";
					if ( (symtab->stroff()+symtab->strsize()) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "string pool extends beyond __LINKEDIT";
					if ( (symtab->stroff() % 4) != 0 ) // work around until rdar://problem/4737991 is fixed
						throw "string pool start not pointer aligned";
					if ( (symtab->strsize() % sizeof(pint_t)) != 0 )	
						throw "string pool size not a multiple of pointer size";
				}
				break;
			case LC_DYSYMTAB:
				{
					if ( isStaticExecutable )
						throw "LC_DYSYMTAB should not be used in static executable";
					foundDynamicSymTab = true;
					fDynamicSymbolTable = (macho_dysymtab_command<P>*)cmd;
					fIndirectTable = (uint32_t*)((char*)fHeader + fDynamicSymbolTable->indirectsymoff());
					fIndirectTableCount = fDynamicSymbolTable->nindirectsyms();
					if ( fIndirectTableCount != 0  ) {
						if ( fDynamicSymbolTable->indirectsymoff() < linkEditSegment->fileoff() )
							throw "indirect symbol table not in __LINKEDIT";
						if ( (fDynamicSymbolTable->indirectsymoff()+fIndirectTableCount*8) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
							throw "indirect symbol table not in __LINKEDIT";
						if ( (fDynamicSymbolTable->indirectsymoff() % sizeof(pint_t)) != 0 )
							throw "indirect symbol table not pointer aligned";
					}
					fLocalRelocationsCount = fDynamicSymbolTable->nlocrel();
					if ( fLocalRelocationsCount != 0 ) {
						fLocalRelocations = (const macho_relocation_info<P>*)((char*)fHeader + fDynamicSymbolTable->locreloff());
						if ( fDynamicSymbolTable->locreloff() < linkEditSegment->fileoff() )
							throw "local relocations not in __LINKEDIT";
						if ( (fDynamicSymbolTable->locreloff()+fLocalRelocationsCount*sizeof(macho_relocation_info<P>)) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
							throw "local relocations not in __LINKEDIT";
						if ( (fDynamicSymbolTable->locreloff() % sizeof(pint_t)) != 0 )
							throw "local relocations table not pointer aligned";
					}
					fExternalRelocationsCount = fDynamicSymbolTable->nextrel();
					if ( fExternalRelocationsCount != 0 ) {
						fExternalRelocations = (const macho_relocation_info<P>*)((char*)fHeader + fDynamicSymbolTable->extreloff());
						if ( fDynamicSymbolTable->extreloff() < linkEditSegment->fileoff() )
							throw "external relocations not in __LINKEDIT";
						if ( (fDynamicSymbolTable->extreloff()+fExternalRelocationsCount*sizeof(macho_relocation_info<P>)) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
							throw "external relocations not in __LINKEDIT";
						if ( (fDynamicSymbolTable->extreloff() % sizeof(pint_t)) != 0 )
							throw "external relocations table not pointer aligned";
					}
				}
				break;
			case LC_SEGMENT_SPLIT_INFO:
				{
					if ( isStaticExecutable )
						throw "LC_SEGMENT_SPLIT_INFO should not be used in static executable";
					const macho_linkedit_data_command<P>* info = (macho_linkedit_data_command<P>*)cmd;
					if ( info->dataoff() < linkEditSegment->fileoff() )
						throw "split seg info not in __LINKEDIT";
					if ( (info->dataoff()+info->datasize()) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "split seg info not in __LINKEDIT";
					if ( (info->dataoff() % sizeof(pint_t)) != 0 )
						throw "split seg info table not pointer aligned";
					if ( (info->datasize() % sizeof(pint_t)) != 0 )
						throw "split seg info size not a multiple of pointer size";
				}
				break;
			case LC_FUNCTION_STARTS:
				{
					const macho_linkedit_data_command<P>* info = (macho_linkedit_data_command<P>*)cmd;
					if ( info->dataoff() < linkEditSegment->fileoff() )
						throw "function starts data not in __LINKEDIT";
					if ( (info->dataoff()+info->datasize()) > (linkEditSegment->fileoff()+linkEditSegment->filesize()) )
						throw "function starts data not in __LINKEDIT";
					if ( (info->dataoff() % sizeof(pint_t)) != 0 )
						throw "function starts data table not pointer aligned";
					if ( (info->datasize() % sizeof(pint_t)) != 0 )
						throw "function starts data size not a multiple of pointer size";
				}
				break;
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	if ( !isStaticExecutable && !foundDynamicSymTab )
		throw "missing dynamic symbol table";
	if ( fStrings == NULL )
		throw "missing symbol table";
		
}

template <typename A>
void MachOChecker<A>::checkSection(const macho_segment_command<P>* segCmd, const macho_section<P>* sect)
{
	uint8_t sectionType = (sect->flags() & SECTION_TYPE);
	if ( sectionType == S_ZEROFILL ) {
		if ( sect->offset() != 0 )
			throwf("section offset should be zero for zero-fill section %s", sect->sectname());
	}
	
	// check section's segment name matches segment
//	if ( strncmp(sect->segname(), segCmd->segname(), 16) != 0 )
//		throwf("section %s in segment %s has wrong segment name", sect->sectname(), segCmd->segname());
	
	// more section tests here
}




template <typename A>
void MachOChecker<A>::checkIndirectSymbolTable()
{
	// static executables don't have indirect symbol table
	if ( fDynamicSymbolTable == NULL )
		return;
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
			for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
				// make sure all magic sections that use indirect symbol table fit within it
				uint32_t start = 0;
				uint32_t elementSize = 0;
				switch ( sect->flags() & SECTION_TYPE ) {
					case S_SYMBOL_STUBS:
						elementSize = sect->reserved2();
						start = sect->reserved1();
						break;
					case S_LAZY_SYMBOL_POINTERS:
					case S_NON_LAZY_SYMBOL_POINTERS:
						elementSize = sizeof(pint_t);
						start = sect->reserved1();
						break;
				}
				if ( elementSize != 0 ) {
					uint32_t count = sect->size() / elementSize;
					if ( (count*elementSize) != sect->size() )
						throwf("%s section size is not an even multiple of element size", sect->sectname());
					if ( (start+count) > fIndirectTableCount )
						throwf("%s section references beyond end of indirect symbol table (%d > %d)", sect->sectname(), start+count, fIndirectTableCount );
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
}




template <typename A>
void MachOChecker<A>::checkSymbolTable()
{
	// verify no duplicate external symbol names
	if ( fDynamicSymbolTable != NULL ) {
		StringSet externalNames;
		const macho_nlist<P>* const	exportedStart = &fSymbols[fDynamicSymbolTable->iextdefsym()];
		const macho_nlist<P>* const exportedEnd = &exportedStart[fDynamicSymbolTable->nextdefsym()];
		int i = fDynamicSymbolTable->iextdefsym();
		for(const macho_nlist<P>* p = exportedStart; p < exportedEnd; ++p, ++i) {
			const char* symName = &fStrings[p->n_strx()];
			if ( symName > fStringsEnd )
				throw "string index out of range";
			//fprintf(stderr, "sym[%d] = %s\n", i, symName);
			if ( externalNames.find(symName) != externalNames.end() )
				throwf("duplicate external symbol: %s", symName);
			if ( (p->n_type() & N_EXT) == 0 )
				throwf("non-external symbol in external symbol range: %s", symName);
			// don't add N_INDR to externalNames because there is likely an undefine with same name
			if ( (p->n_type() & N_INDR) == 0 )
				externalNames.insert(symName);
		}
		// verify no undefines with same name as an external symbol
		const macho_nlist<P>* const	undefinesStart = &fSymbols[fDynamicSymbolTable->iundefsym()];
		const macho_nlist<P>* const undefinesEnd = &undefinesStart[fDynamicSymbolTable->nundefsym()];
		for(const macho_nlist<P>* p = undefinesStart; p < undefinesEnd; ++p) {
			const char* symName = &fStrings[p->n_strx()];
			if ( symName > fStringsEnd )
				throw "string index out of range";
			if ( externalNames.find(symName) != externalNames.end() )
				throwf("undefine with same name as external symbol: %s", symName);
		}
		// verify all N_SECT values are valid
		for(const macho_nlist<P>* p = fSymbols; p < &fSymbols[fSymbolCount]; ++p) {
			uint8_t type = p->n_type();
			if ( ((type & N_STAB) == 0) && ((type & N_TYPE) == N_SECT) ) {
				if ( p->n_sect() > fSectionCount ) {
					throwf("symbol '%s' has n_sect=%d which is too large", &fStrings[p->n_strx()], p->n_sect());
				}
			}
		}
	}
}


template <typename A>
void MachOChecker<A>::checkInitTerms()
{
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
			for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
				// make sure all magic sections that use indirect symbol table fit within it
				uint32_t count;
				pint_t* arrayStart;
				pint_t* arrayEnd;
				const char* kind = "initializer";
				switch ( sect->flags() & SECTION_TYPE ) {
					case S_MOD_TERM_FUNC_POINTERS:
						kind = "terminator";
						// fall through
					case S_MOD_INIT_FUNC_POINTERS:
						count = sect->size() / sizeof(pint_t);
						if ( (count*sizeof(pint_t)) != sect->size() )
							throwf("%s section size is not an even multiple of element size", sect->sectname());
						if ( (sect->addr() % sizeof(pint_t)) != 0 )
							throwf("%s section size is not pointer size aligned", sect->sectname());
						// check each pointer in array points within TEXT
						arrayStart = (pint_t*)((char*)fHeader + sect->offset());
						arrayEnd = (pint_t*)((char*)fHeader + sect->offset() + sect->size());
						for (pint_t* p=arrayStart; p < arrayEnd; ++p) {
							pint_t pointer = P::getP(*p);
							if ( (pointer < fTEXTSegment->vmaddr()) ||  (pointer >= (fTEXTSegment->vmaddr()+fTEXTSegment->vmsize())) ) 
								throwf("%s 0x%08llX points outside __TEXT segment", kind, (long long)pointer);
						}
						// check each pointer in array will be rebased and not bound
						if ( fSlidableImage ) {
							pint_t sectionBeginAddr = sect->addr();
							pint_t sectionEndddr = sect->addr() + sect->size();
							for(pint_t addr = sectionBeginAddr; addr < sectionEndddr; addr += sizeof(pint_t)) {
								if ( addressIsBindingSite(addr) )
									throwf("%s at 0x%0llX has binding to external symbol", kind, (long long)addr);
								if ( ! addressIsRebaseSite(addr) )
									throwf("%s at 0x%0llX is not rebased", kind, (long long)addr);
							}
						}
						break;
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}

}


template <>
ppc::P::uint_t MachOChecker<ppc>::relocBase()
{
	if ( fHeader->flags() & MH_SPLIT_SEGS )
		return fFirstWritableSegment->vmaddr();
	else
		return fFirstSegment->vmaddr();
}

template <>
ppc64::P::uint_t MachOChecker<ppc64>::relocBase()
{
	if ( fWriteableSegmentWithAddrOver4G ) 
		return fFirstWritableSegment->vmaddr();
	else
		return fFirstSegment->vmaddr();
}

template <>
x86::P::uint_t MachOChecker<x86>::relocBase()
{
	if ( fHeader->flags() & MH_SPLIT_SEGS )
		return fFirstWritableSegment->vmaddr();
	else
		return fFirstSegment->vmaddr();
}

template <>
x86_64::P::uint_t MachOChecker<x86_64>::relocBase()
{
	// check for split-seg
	return fFirstWritableSegment->vmaddr();
}

template <>
arm::P::uint_t MachOChecker<arm>::relocBase()
{
	if ( fHeader->flags() & MH_SPLIT_SEGS )
		return fFirstWritableSegment->vmaddr();
	else
		return fFirstSegment->vmaddr();
}


template <typename A>
bool MachOChecker<A>::addressInWritableSegment(pint_t address)
{
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			if ( (address >= segCmd->vmaddr()) && (address < segCmd->vmaddr()+segCmd->vmsize()) ) {
				// if segment is writable, we are fine
				if ( (segCmd->initprot() & VM_PROT_WRITE) != 0 ) 
					return true;
				// could be a text reloc, make sure section bit is set
				const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
				const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
				for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
					if ( (sect->addr() <= address) && (address < (sect->addr()+sect->size())) ) {
						// found section for this address, if has relocs we are fine
						return ( (sect->flags() & (S_ATTR_EXT_RELOC|S_ATTR_LOC_RELOC)) != 0 );
					}
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
	return false;
}


template <>
void MachOChecker<ppc>::checkExternalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 2 ) 
		throw "bad external relocation length";
	if ( reloc->r_type() != GENERIC_RELOC_VANILLA ) 
		throw "unknown external relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad external relocation pc_rel";
	if ( reloc->r_extern() == 0 )
		throw "local relocation found with external relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "external relocation address not in writable segment";
	// FIX: check r_symbol
}

template <>
void MachOChecker<ppc64>::checkExternalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 3 ) 
		throw "bad external relocation length";
	if ( reloc->r_type() != GENERIC_RELOC_VANILLA ) 
		throw "unknown external relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad external relocation pc_rel";
	if ( reloc->r_extern() == 0 )
		throw "local relocation found with external relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "external relocation address not in writable segment";
	// FIX: check r_symbol
}

template <>
void MachOChecker<x86>::checkExternalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 2 ) 
		throw "bad external relocation length";
	if ( reloc->r_type() != GENERIC_RELOC_VANILLA ) 
		throw "unknown external relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad external relocation pc_rel";
	if ( reloc->r_extern() == 0 )
		throw "local relocation found with external relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "external relocation address not in writable segment";
	// FIX: check r_symbol
}


template <>
void MachOChecker<x86_64>::checkExternalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 3 ) 
		throw "bad external relocation length";
	if ( reloc->r_type() != X86_64_RELOC_UNSIGNED ) 
		throw "unknown external relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad external relocation pc_rel";
	if ( reloc->r_extern() == 0 ) 
		throw "local relocation found with external relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "exernal relocation address not in writable segment";
	// FIX: check r_symbol
}

template <>
void MachOChecker<arm>::checkExternalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 2 ) 
		throw "bad external relocation length";
	if ( reloc->r_type() != ARM_RELOC_VANILLA ) 
		throw "unknown external relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad external relocation pc_rel";
	if ( reloc->r_extern() == 0 )
		throw "local relocation found with external relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "external relocation address not in writable segment";
	// FIX: check r_symbol
}


template <>
void MachOChecker<ppc>::checkLocalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_address() & R_SCATTERED ) {
		// scattered
		const macho_scattered_relocation_info<P>* sreloc = (const macho_scattered_relocation_info<P>*)reloc;
		// FIX
	
	}
	else {
		// ignore pair relocs
		if ( reloc->r_type() == PPC_RELOC_PAIR ) 
			return;
		// FIX
		if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
			throwf("local relocation address 0x%08X not in writable segment", reloc->r_address());
	}
}


template <>
void MachOChecker<ppc64>::checkLocalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 3 ) 
		throw "bad local relocation length";
	if ( reloc->r_type() != GENERIC_RELOC_VANILLA ) 
		throw "unknown local relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad local relocation pc_rel";
	if ( reloc->r_extern() != 0 ) 
		throw "external relocation found with local relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "local relocation address not in writable segment";
}

template <>
void MachOChecker<x86>::checkLocalReloation(const macho_relocation_info<P>* reloc)
{
	// FIX
}

template <>
void MachOChecker<x86_64>::checkLocalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_length() != 3 ) 
		throw "bad local relocation length";
	if ( reloc->r_type() != X86_64_RELOC_UNSIGNED ) 
		throw "unknown local relocation type";
	if ( reloc->r_pcrel() != 0 ) 
		throw "bad local relocation pc_rel";
	if ( reloc->r_extern() != 0 ) 
		throw "external relocation found with local relocations";
	if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
		throw "local relocation address not in writable segment";
}

template <>
void MachOChecker<arm>::checkLocalReloation(const macho_relocation_info<P>* reloc)
{
	if ( reloc->r_address() & R_SCATTERED ) {
		// scattered
		const macho_scattered_relocation_info<P>* sreloc = (const macho_scattered_relocation_info<P>*)reloc;
		if ( sreloc->r_length() != 2 ) 
			throw "bad local scattered relocation length";
		if ( sreloc->r_type() != ARM_RELOC_PB_LA_PTR ) 
			throw "bad local scattered relocation type";
	}
	else {
		if ( reloc->r_length() != 2 ) 
			throw "bad local relocation length";
		if ( reloc->r_extern() != 0 ) 
			throw "external relocation found with local relocations";
		if ( ! this->addressInWritableSegment(reloc->r_address() + this->relocBase()) )
			throw "local relocation address not in writable segment";
	}
}

template <typename A>
void MachOChecker<A>::checkRelocations()
{
	// external relocations should be sorted to minimize dyld symbol lookups
	// therefore every reloc with the same r_symbolnum value should be contiguous 
	std::set<uint32_t> previouslySeenSymbolIndexes;
	uint32_t lastSymbolIndex = 0xFFFFFFFF;
	const macho_relocation_info<P>* const externRelocsEnd = &fExternalRelocations[fExternalRelocationsCount];
	for (const macho_relocation_info<P>* reloc = fExternalRelocations; reloc < externRelocsEnd; ++reloc) {
		this->checkExternalReloation(reloc);
		if ( reloc->r_symbolnum() != lastSymbolIndex ) {
			if ( previouslySeenSymbolIndexes.count(reloc->r_symbolnum()) != 0 )
				throw "external relocations not sorted";
			previouslySeenSymbolIndexes.insert(lastSymbolIndex);
			lastSymbolIndex = reloc->r_symbolnum();
		}
	}
	
	const macho_relocation_info<P>* const localRelocsEnd = &fLocalRelocations[fLocalRelocationsCount];
	for (const macho_relocation_info<P>* reloc = fLocalRelocations; reloc < localRelocsEnd; ++reloc) {
		this->checkLocalReloation(reloc);
	}
	
	// verify any section with S_ATTR_LOC_RELOC bits set actually has text relocs
	const macho_load_command<P>* const cmds = (macho_load_command<P>*)((uint8_t*)fHeader + sizeof(macho_header<P>));
	const uint32_t cmd_count = fHeader->ncmds();
	const macho_load_command<P>* cmd = cmds;
	for (uint32_t i = 0; i < cmd_count; ++i) {
		if ( cmd->cmd() == macho_segment_command<P>::CMD ) {
			const macho_segment_command<P>* segCmd = (const macho_segment_command<P>*)cmd;
			// if segment is writable, we are fine
			if ( (segCmd->initprot() & VM_PROT_WRITE) != 0 ) 
				continue;
			// look at sections that have text reloc bit set
			const macho_section<P>* const sectionsStart = (macho_section<P>*)((char*)segCmd + sizeof(macho_segment_command<P>));
			const macho_section<P>* const sectionsEnd = &sectionsStart[segCmd->nsects()];
			for(const macho_section<P>* sect = sectionsStart; sect < sectionsEnd; ++sect) {
				if ( (sect->flags() & S_ATTR_LOC_RELOC) != 0 ) {
					if ( ! hasTextRelocInRange(sect->addr(), sect->addr()+sect->size()) ) {
						throwf("section %s has attribute set that it has relocs, but it has none", sect->sectname());
					}
				}
			}
		}
		cmd = (const macho_load_command<P>*)(((uint8_t*)cmd)+cmd->cmdsize());
	}
}

template <typename A>
typename A::P::uint_t MachOChecker<A>::segStartAddress(uint8_t segIndex)
{
	if ( segIndex > fSegments.size() )
		throw "segment index out of range";
	return fSegments[segIndex]->vmaddr();
}

template <typename A>
bool MachOChecker<A>::hasTextRelocInRange(pint_t rangeStart, pint_t rangeEnd)
{
	// look at local relocs
	const macho_relocation_info<P>* const localRelocsEnd = &fLocalRelocations[fLocalRelocationsCount];
	for (const macho_relocation_info<P>* reloc = fLocalRelocations; reloc < localRelocsEnd; ++reloc) {
		pint_t relocAddress = reloc->r_address() + this->relocBase();
		if ( (rangeStart <= relocAddress) && (relocAddress < rangeEnd) )
			return true;
	}	
	// look rebase info
	if ( fDyldInfo != NULL ) {
		const uint8_t* p = (uint8_t*)fHeader + fDyldInfo->rebase_off();
		const uint8_t* end = &p[fDyldInfo->rebase_size()];
		
		uint8_t type = 0;
		uint64_t segOffset = 0;
		uint32_t count;
		uint32_t skip;
		int segIndex;
		pint_t segStartAddr = 0;
		pint_t addr;
		bool done = false;
		while ( !done && (p < end) ) {
			uint8_t immediate = *p & REBASE_IMMEDIATE_MASK;
			uint8_t opcode = *p & REBASE_OPCODE_MASK;
			++p;
			switch (opcode) {
				case REBASE_OPCODE_DONE:
					done = true;
					break;
				case REBASE_OPCODE_SET_TYPE_IMM:
					type = immediate;
					break;
				case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
					segIndex = immediate;
					segStartAddr = segStartAddress(segIndex);
					segOffset = read_uleb128(p, end);
					break;
				case REBASE_OPCODE_ADD_ADDR_ULEB:
					segOffset += read_uleb128(p, end);
					break;
				case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
					segOffset += immediate*sizeof(pint_t);
					break;
				case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
					for (int i=0; i < immediate; ++i) {
						addr = segStartAddr+segOffset;
						if ( (rangeStart <= addr) && (addr < rangeEnd) )
							return true;
						//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
						segOffset += sizeof(pint_t);
					}
					break;
				case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
					count = read_uleb128(p, end);
					for (uint32_t i=0; i < count; ++i) {
						addr = segStartAddr+segOffset;
						if ( (rangeStart <= addr) && (addr < rangeEnd) )
							return true;
						//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
						segOffset += sizeof(pint_t);
					}
					break;
				case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
					addr = segStartAddr+segOffset;
					if ( (rangeStart <= addr) && (addr < rangeEnd) )
						return true;
					//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
					segOffset += read_uleb128(p, end) + sizeof(pint_t);
					break;
				case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
					count = read_uleb128(p, end);
					skip = read_uleb128(p, end);
					for (uint32_t i=0; i < count; ++i) {
						addr = segStartAddr+segOffset;
						if ( (rangeStart <= addr) && (addr < rangeEnd) )
							return true;
						//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
						segOffset += skip + sizeof(pint_t);
					}
					break;
				default:
					throwf("bad rebase opcode %d", *p);
			}
		}	
	}
}

template <typename A>
bool MachOChecker<A>::addressIsRebaseSite(pint_t targetAddr)
{
	// look at local relocs
	const macho_relocation_info<P>* const localRelocsEnd = &fLocalRelocations[fLocalRelocationsCount];
	for (const macho_relocation_info<P>* reloc = fLocalRelocations; reloc < localRelocsEnd; ++reloc) {
		pint_t relocAddress = reloc->r_address() + this->relocBase();
		if ( relocAddress == targetAddr )
			return true;
	}	
	// look rebase info
	if ( fDyldInfo != NULL ) {
		const uint8_t* p = (uint8_t*)fHeader + fDyldInfo->rebase_off();
		const uint8_t* end = &p[fDyldInfo->rebase_size()];
		
		uint8_t type = 0;
		uint64_t segOffset = 0;
		uint32_t count;
		uint32_t skip;
		int segIndex;
		pint_t segStartAddr = 0;
		pint_t addr;
		bool done = false;
		while ( !done && (p < end) ) {
			uint8_t immediate = *p & REBASE_IMMEDIATE_MASK;
			uint8_t opcode = *p & REBASE_OPCODE_MASK;
			++p;
			switch (opcode) {
				case REBASE_OPCODE_DONE:
					done = true;
					break;
				case REBASE_OPCODE_SET_TYPE_IMM:
					type = immediate;
					break;
				case REBASE_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
					segIndex = immediate;
					segStartAddr = segStartAddress(segIndex);
					segOffset = read_uleb128(p, end);
					break;
				case REBASE_OPCODE_ADD_ADDR_ULEB:
					segOffset += read_uleb128(p, end);
					break;
				case REBASE_OPCODE_ADD_ADDR_IMM_SCALED:
					segOffset += immediate*sizeof(pint_t);
					break;
				case REBASE_OPCODE_DO_REBASE_IMM_TIMES:
					for (int i=0; i < immediate; ++i) {
						addr = segStartAddr+segOffset;
						if ( addr == targetAddr )
							return true;
						//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
						segOffset += sizeof(pint_t);
					}
					break;
				case REBASE_OPCODE_DO_REBASE_ULEB_TIMES:
					count = read_uleb128(p, end);
					for (uint32_t i=0; i < count; ++i) {
						addr = segStartAddr+segOffset;
						if ( addr == targetAddr )
							return true;
						//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
						segOffset += sizeof(pint_t);
					}
					break;
				case REBASE_OPCODE_DO_REBASE_ADD_ADDR_ULEB:
					addr = segStartAddr+segOffset;
					if ( addr == targetAddr )
						return true;
					//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
					segOffset += read_uleb128(p, end) + sizeof(pint_t);
					break;
				case REBASE_OPCODE_DO_REBASE_ULEB_TIMES_SKIPPING_ULEB:
					count = read_uleb128(p, end);
					skip = read_uleb128(p, end);
					for (uint32_t i=0; i < count; ++i) {
						addr = segStartAddr+segOffset;
						if ( addr == targetAddr )
							return true;
						//printf("%-7s %-16s 0x%08llX  %s\n", segName, sectionName(segIndex, segStartAddr+segOffset), segStartAddr+segOffset, typeName);
						segOffset += skip + sizeof(pint_t);
					}
					break;
				default:
					throwf("bad rebase opcode %d", *p);
			}
		}	
	}
	return false;
}


template <typename A>
bool MachOChecker<A>::addressIsBindingSite(pint_t targetAddr)
{
	// look at external relocs
	const macho_relocation_info<P>* const externRelocsEnd = &fExternalRelocations[fExternalRelocationsCount];
	for (const macho_relocation_info<P>* reloc = fExternalRelocations; reloc < externRelocsEnd; ++reloc) {
		pint_t relocAddress = reloc->r_address() + this->relocBase();
		if ( relocAddress == targetAddr )
			return true;
	}	
	// look bind info
	if ( fDyldInfo != NULL ) {
		const uint8_t* p = (uint8_t*)fHeader + fDyldInfo->bind_off();
		const uint8_t* end = &p[fDyldInfo->bind_size()];
		
		uint8_t type = 0;
		uint64_t segOffset = 0;
		uint32_t count;
		uint32_t skip;
		uint8_t flags;
		const char* symbolName = NULL;
		int libraryOrdinal = 0;
		int segIndex;
		int64_t addend = 0;
		pint_t segStartAddr = 0;
		pint_t addr;
		bool done = false;
		while ( !done && (p < end) ) {
			uint8_t immediate = *p & BIND_IMMEDIATE_MASK;
			uint8_t opcode = *p & BIND_OPCODE_MASK;
			++p;
			switch (opcode) {
				case BIND_OPCODE_DONE:
					done = true;
					break;
				case BIND_OPCODE_SET_DYLIB_ORDINAL_IMM:
					libraryOrdinal = immediate;
					break;
				case BIND_OPCODE_SET_DYLIB_ORDINAL_ULEB:
					libraryOrdinal = read_uleb128(p, end);
					break;
				case BIND_OPCODE_SET_DYLIB_SPECIAL_IMM:
					// the special ordinals are negative numbers
					if ( immediate == 0 )
						libraryOrdinal = 0;
					else {
						int8_t signExtended = BIND_OPCODE_MASK | immediate;
						libraryOrdinal = signExtended;
					}
					break;
				case BIND_OPCODE_SET_SYMBOL_TRAILING_FLAGS_IMM:
					symbolName = (char*)p;
					while (*p != '\0')
						++p;
					++p;
					break;
				case BIND_OPCODE_SET_TYPE_IMM:
					type = immediate;
					break;
				case BIND_OPCODE_SET_ADDEND_SLEB:
					addend = read_sleb128(p, end);
					break;
				case BIND_OPCODE_SET_SEGMENT_AND_OFFSET_ULEB:
					segIndex = immediate;
					segStartAddr = segStartAddress(segIndex);
					segOffset = read_uleb128(p, end);
					break;
				case BIND_OPCODE_ADD_ADDR_ULEB:
					segOffset += read_uleb128(p, end);
					break;
				case BIND_OPCODE_DO_BIND:
					if ( (segStartAddr+segOffset) == targetAddr )
						return true;
					segOffset += sizeof(pint_t);
					break;
				case BIND_OPCODE_DO_BIND_ADD_ADDR_ULEB:
					if ( (segStartAddr+segOffset) == targetAddr )
						return true;
					segOffset += read_uleb128(p, end) + sizeof(pint_t);
					break;
				case BIND_OPCODE_DO_BIND_ADD_ADDR_IMM_SCALED:
					if ( (segStartAddr+segOffset) == targetAddr )
						return true;
					segOffset += immediate*sizeof(pint_t) + sizeof(pint_t);
					break;
				case BIND_OPCODE_DO_BIND_ULEB_TIMES_SKIPPING_ULEB:
					count = read_uleb128(p, end);
					skip = read_uleb128(p, end);
					for (uint32_t i=0; i < count; ++i) {
						if ( (segStartAddr+segOffset) == targetAddr )
							return true;
						segOffset += skip + sizeof(pint_t);
					}
					break;
				default:
					throwf("bad bind opcode %d", *p);
			}
		}	
	}
	return false;
}


static void check(const char* path)
{
	struct stat stat_buf;
	
	try {
		int fd = ::open(path, O_RDONLY, 0);
		if ( fd == -1 )
			throw "cannot open file";
		if ( ::fstat(fd, &stat_buf) != 0 ) 
			throwf("fstat(%s) failed, errno=%d\n", path, errno);
		uint32_t length = stat_buf.st_size;
		uint8_t* p = (uint8_t*)::mmap(NULL, stat_buf.st_size, PROT_READ, MAP_FILE | MAP_PRIVATE, fd, 0);
		if ( p == ((uint8_t*)(-1)) )
			throw "cannot map file";
		::close(fd);
		const mach_header* mh = (mach_header*)p;
		if ( mh->magic == OSSwapBigToHostInt32(FAT_MAGIC) ) {
			const struct fat_header* fh = (struct fat_header*)p;
			const struct fat_arch* archs = (struct fat_arch*)(p + sizeof(struct fat_header));
			for (unsigned long i=0; i < OSSwapBigToHostInt32(fh->nfat_arch); ++i) {
				size_t offset = OSSwapBigToHostInt32(archs[i].offset);
				size_t size = OSSwapBigToHostInt32(archs[i].size);
				unsigned int cputype = OSSwapBigToHostInt32(archs[i].cputype);

				switch(cputype) {
				case CPU_TYPE_POWERPC:
					if ( MachOChecker<ppc>::validFile(p + offset) )
						MachOChecker<ppc>::make(p + offset, size, path);
					else
						throw "in universal file, ppc slice does not contain ppc mach-o";
					break;
				case CPU_TYPE_I386:
					if ( MachOChecker<x86>::validFile(p + offset) )
						MachOChecker<x86>::make(p + offset, size, path);
					else
						throw "in universal file, i386 slice does not contain i386 mach-o";
					break;
				case CPU_TYPE_POWERPC64:
					if ( MachOChecker<ppc64>::validFile(p + offset) )
						MachOChecker<ppc64>::make(p + offset, size, path);
					else
						throw "in universal file, ppc64 slice does not contain ppc64 mach-o";
					break;
				case CPU_TYPE_X86_64:
					if ( MachOChecker<x86_64>::validFile(p + offset) )
						MachOChecker<x86_64>::make(p + offset, size, path);
					else
						throw "in universal file, x86_64 slice does not contain x86_64 mach-o";
					break;
				case CPU_TYPE_ARM:
					if ( MachOChecker<arm>::validFile(p + offset) )
						MachOChecker<arm>::make(p + offset, size, path);
					else
						throw "in universal file, arm slice does not contain arm mach-o";
					break;
				default:
						throwf("in universal file, unknown architecture slice 0x%x\n", cputype);
				}
			}
		}
		else if ( MachOChecker<x86>::validFile(p) ) {
			MachOChecker<x86>::make(p, length, path);
		}
		else if ( MachOChecker<ppc>::validFile(p) ) {
			MachOChecker<ppc>::make(p, length, path);
		}
		else if ( MachOChecker<ppc64>::validFile(p) ) {
			MachOChecker<ppc64>::make(p, length, path);
		}
		else if ( MachOChecker<x86_64>::validFile(p) ) {
			MachOChecker<x86_64>::make(p, length, path);
		}
		else if ( MachOChecker<arm>::validFile(p) ) {
			MachOChecker<arm>::make(p, length, path);
		}
		else {
			throw "not a known file type";
		}
	}
	catch (const char* msg) {
		throwf("%s in %s", msg, path);
	}
}


int main(int argc, const char* argv[])
{
	bool progress = false;
	int result = 0;
	for(int i=1; i < argc; ++i) {
		const char* arg = argv[i];
		if ( arg[0] == '-' ) {
			if ( strcmp(arg, "-progress") == 0 ) {
				progress = true;
			}
			else {
				throwf("unknown option: %s\n", arg);
			}
		}
		else {
			bool success = true;
			try {
				check(arg);
			}
			catch (const char* msg) {
				fprintf(stderr, "machocheck failed: %s %s\n", arg, msg);
				result = 1;
				success = false;
			}
			if ( success && progress )
				printf("ok: %s\n", arg);
		}
	}
	
	return result;
}



