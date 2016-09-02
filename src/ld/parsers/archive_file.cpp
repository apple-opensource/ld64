/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2005-2011 Apple Inc. All rights reserved.
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

#include <stdint.h>
#include <math.h>
#include <unistd.h>
#include <sys/param.h>
#include <mach-o/ranlib.h>
#include <ar.h>

#include <vector>
#include <set>
#include <map>
#include <algorithm>
#include <ext/hash_map>

#include "MachOFileAbstraction.hpp"
#include "Architectures.hpp"

#include "macho_relocatable_file.h"
#include "lto_file.h"
#include "archive_file.h"


namespace archive {

typedef const struct ranlib* ConstRanLibPtr;

// forward reference
template <typename A> class File;


template <typename A>
class Parser 
{
public:
	typedef typename A::P					P;

	static bool										validFile(const uint8_t* fileContent, uint64_t fileLength, 
																const mach_o::relocatable::ParserOptions& opts) {
														return File<A>::validFile(fileContent, fileLength, opts); }
	static File<A>*									parse(const uint8_t* fileContent, uint64_t fileLength, 
															const char* path, time_t mTime, 
															uint32_t ordinal, const ParserOptions& opts) {
															 return new File<A>(fileContent, fileLength, path, mTime,
																			ordinal, opts);
														}

};

template <typename A>
class File : public ld::archive::File
{
public:
	static bool										validFile(const uint8_t* fileContent, uint64_t fileLength,
																const mach_o::relocatable::ParserOptions& opts);
													File(const uint8_t* fileContent, uint64_t fileLength,
															const char* pth, time_t modTime, 
															uint32_t ord, const ParserOptions& opts);
	virtual											~File() {}

	// overrides of ld::File
	virtual bool										forEachAtom(ld::File::AtomHandler&) const;
	virtual bool										justInTimeforEachAtom(const char* name, ld::File::AtomHandler&) const;
	virtual uint32_t									subFileCount() const  { return _archiveFilelength/sizeof(ar_hdr); }
	
	// overrides of ld::archive::File
	virtual bool										justInTimeDataOnlyforEachAtom(const char* name, ld::File::AtomHandler& handler) const;

private:
	static bool										validMachOFile(const uint8_t* fileContent, uint64_t fileLength, 
																	const mach_o::relocatable::ParserOptions& opts);
	static bool										validLTOFile(const uint8_t* fileContent, uint64_t fileLength, 
																	const mach_o::relocatable::ParserOptions& opts);
	static cpu_type_t								architecture();

	class Entry : ar_hdr
	{
	public:
		const char*			name() const;
		time_t				modificationTime() const;
		const uint8_t*		content() const;
		uint32_t			contentSize() const;
		const Entry*		next() const;
	private:
		bool				hasLongName() const;
		unsigned int		getLongNameSpace() const;

	};

	class CStringEquals
	{
	public:
		bool operator()(const char* left, const char* right) const { return (strcmp(left, right) == 0); }
	};
	typedef __gnu_cxx::hash_map<const char*, const struct ranlib*, __gnu_cxx::hash<const char*>, CStringEquals> NameToEntryMap;

	typedef typename A::P							P;
	typedef typename A::P::E						E;

	struct MemberState { ld::relocatable::File* file; bool logged; bool loaded; };
	
	typedef std::map<const class Entry*, MemberState> MemberToStateMap;

	const struct ranlib*							ranlibHashSearch(const char* name) const;
	MemberState&									makeObjectFileForMember(const Entry* member) const;
	bool											memberHasObjCCategories(const Entry* member) const;
	void											dumpTableOfContents();
	void											buildHashTable();

	const uint8_t*									_archiveFileContent;
	uint64_t										_archiveFilelength;
	const struct ranlib*							_tableOfContents;
	uint32_t										_tableOfContentCount;
	const char*										_tableOfContentStrings;
	mutable MemberToStateMap						_instantiatedEntries;
	NameToEntryMap									_hashTable;
	const bool										_forceLoadAll;
	const bool										_forceLoadObjC;
	const bool										_forceLoadThis;
	const bool										_objc2ABI;
	const bool										_verboseLoad;
	const bool										_logAllFiles;
	const mach_o::relocatable::ParserOptions		_objOpts;
};


template <typename A>
bool File<A>::Entry::hasLongName() const
{
	return ( strncmp(this->ar_name, AR_EFMT1, strlen(AR_EFMT1)) == 0 );
}

template <typename A>
unsigned int File<A>::Entry::getLongNameSpace() const
{
	char* endptr;
	long result = strtol(&this->ar_name[strlen(AR_EFMT1)], &endptr, 10);
	return result;
}

template <typename A>
const char* File<A>::Entry::name() const
{
	if ( this->hasLongName() ) {
		int len = this->getLongNameSpace();
		static char longName[256];
		strncpy(longName, ((char*)this)+sizeof(ar_hdr), len);
		longName[len] = '\0';
		return longName;
	}
	else {
		static char shortName[20];
		strncpy(shortName, this->ar_name, 16);
		shortName[16] = '\0';
		char* space = strchr(shortName, ' ');
		if ( space != NULL )
			*space = '\0';
		return shortName;
	}
}

template <typename A>
time_t	File<A>::Entry::modificationTime() const
{
	char temp[14];
	strncpy(temp, this->ar_date, 12);
	temp[12] = '\0';
	char* endptr;
	return (time_t)strtol(temp, &endptr, 10);
}


template <typename A>
const uint8_t* File<A>::Entry::content() const
{
	if ( this->hasLongName() )
		return ((uint8_t*)this) + sizeof(ar_hdr) + this->getLongNameSpace();
	else
		return ((uint8_t*)this) + sizeof(ar_hdr);
}


template <typename A>
uint32_t File<A>::Entry::contentSize() const
{
	char temp[12];
	strncpy(temp, this->ar_size, 10);
	temp[10] = '\0';
	char* endptr;
	long size = strtol(temp, &endptr, 10);
	// long name is included in ar_size
	if ( this->hasLongName() )
		size -= this->getLongNameSpace();
	return size;
}


template <typename A>
const class File<A>::Entry* File<A>::Entry::next() const
{
	const uint8_t* p = this->content() + contentSize();
	p = (const uint8_t*)(((uintptr_t)p+3) & (-4));  // 4-byte align
	return (class File<A>::Entry*)p;
}


template <> cpu_type_t File<ppc>::architecture()    { return CPU_TYPE_POWERPC; }
template <> cpu_type_t File<ppc64>::architecture()  { return CPU_TYPE_POWERPC64; }
template <> cpu_type_t File<x86>::architecture()    { return CPU_TYPE_I386; }
template <> cpu_type_t File<x86_64>::architecture() { return CPU_TYPE_X86_64; }
template <> cpu_type_t File<arm>::architecture()    { return CPU_TYPE_ARM; }


template <typename A>
bool File<A>::validMachOFile(const uint8_t* fileContent, uint64_t fileLength, const mach_o::relocatable::ParserOptions& opts)
{	
	return mach_o::relocatable::isObjectFile(fileContent, fileLength, opts);
}

template <typename A>
bool File<A>::validLTOFile(const uint8_t* fileContent, uint64_t fileLength, const mach_o::relocatable::ParserOptions& opts)
{
	return lto::isObjectFile(fileContent, fileLength, opts.architecture, opts.subType);
}



template <typename A>
bool File<A>::validFile(const uint8_t* fileContent, uint64_t fileLength, const mach_o::relocatable::ParserOptions& opts)
{
	// must have valid archive header
	if ( strncmp((const char*)fileContent, "!<arch>\n", 8) != 0 )
		return false;
		
	// peak at first .o file and verify it is correct architecture
	const Entry* const start = (Entry*)&fileContent[8];
	const Entry* const end = (Entry*)&fileContent[fileLength];
	for (const Entry* p=start; p < end; p = p->next()) {
		const char* memberName = p->name();
		// skip option table-of-content member
		if ( (p==start) && ((strcmp(memberName, SYMDEF_SORTED) == 0) || (strcmp(memberName, SYMDEF) == 0)) )
			continue;
		// archive is valid if first .o file is valid
		return (validMachOFile(p->content(), p->contentSize(), opts) || validLTOFile(p->content(), p->contentSize(), opts));
	}	
	// empty archive
	return true;
}


template <typename A>
File<A>::File(const uint8_t fileContent[], uint64_t fileLength, const char* pth, time_t modTime, 
					uint32_t ord, const ParserOptions& opts)
 : ld::archive::File(strdup(pth), modTime, ord),
	_archiveFileContent(fileContent), _archiveFilelength(fileLength), 
	_tableOfContents(NULL), _tableOfContentCount(0), _tableOfContentStrings(NULL), 
	_forceLoadAll(opts.forceLoadAll), _forceLoadObjC(opts.forceLoadObjC), 
	_forceLoadThis(opts.forceLoadThisArchive), _objc2ABI(opts.objcABI2), _verboseLoad(opts.verboseLoad), 
	_logAllFiles(opts.logAllFiles), _objOpts(opts.objOpts)
{
	if ( strncmp((const char*)fileContent, "!<arch>\n", 8) != 0 )
		throw "not an archive";

	if ( !_forceLoadAll ) {
		const Entry* const firstMember = (Entry*)&_archiveFileContent[8];
		if ( (strcmp(firstMember->name(), SYMDEF_SORTED) == 0) || (strcmp(firstMember->name(), SYMDEF) == 0) ) {
			const uint8_t* contents = firstMember->content();
			uint32_t ranlibArrayLen = E::get32(*((uint32_t*)contents));
			_tableOfContents = (const struct ranlib*)&contents[4];
			_tableOfContentCount = ranlibArrayLen / sizeof(struct ranlib);
			_tableOfContentStrings = (const char*)&contents[ranlibArrayLen+8];
			if ( ((uint8_t*)(&_tableOfContents[_tableOfContentCount]) > &fileContent[fileLength])
				|| ((uint8_t*)_tableOfContentStrings > &fileContent[fileLength]) )
				throw "malformed archive, perhaps wrong architecture";
			this->buildHashTable();
		}
		else
			throw "archive has no table of contents";
	}
}

template <>
bool File<x86>::memberHasObjCCategories(const Entry* member) const
{
	if ( _objc2ABI ) {	
		// i386 for iOS simulator uses ObjC2 which has no global symbol for categories
		return mach_o::relocatable::hasObjC2Categories(member->content());
	}
	else {
		// i386 uses ObjC1 ABI which has .objc_category* global symbols
		return false;
	}
}

template <>
bool File<ppc>::memberHasObjCCategories(const Entry* member) const
{
	// ppc uses ObjC1 ABI which has .objc_category* global symbols
	return false;
}


template <typename A>
bool File<A>::memberHasObjCCategories(const Entry* member) const
{
	// x86_64 and ARM use ObjC2 which has no global symbol for categories
	return mach_o::relocatable::hasObjC2Categories(member->content());
}


template <typename A>
typename File<A>::MemberState& File<A>::makeObjectFileForMember(const Entry* member) const
{
	// in case member was instantiated earlier but not needed yet
	typename MemberToStateMap::iterator pos = _instantiatedEntries.find(member);
	if ( pos != _instantiatedEntries.end() )
		return pos->second;

	const char* memberName = member->name();
	char memberPath[strlen(this->path()) + strlen(memberName)+4];
	strcpy(memberPath, this->path());
	strcat(memberPath, "(");
	strcat(memberPath, memberName);
	strcat(memberPath, ")");
	//fprintf(stderr, "using %s from %s\n", memberName, this->path());
	try {
		// range check
		if ( member > (Entry*)(_archiveFileContent+_archiveFilelength) )
			throwf("corrupt archive, member starts past end of file");										
		if ( (member->content() + member->contentSize()) > (_archiveFileContent+_archiveFilelength) )
			throwf("corrupt archive, member contents extends past end of file");										
		const char* mPath = strdup(memberPath);
		// offset the ordinals in this mach-o .o file, so that atoms layout in same order as in archive
		uint32_t memberIndex = ((uint8_t*)member - _archiveFileContent)/sizeof(ar_hdr);
		// see if member is mach-o file
		ld::relocatable::File* result = mach_o::relocatable::parse(member->content(), member->contentSize(), 
																	mPath, member->modificationTime(), 
																	this->ordinal() + memberIndex, _objOpts);
		if ( result != NULL ) {
			MemberState state = {result, false, false};
			_instantiatedEntries[member] = state;
			return _instantiatedEntries[member];
		}
		// see if member is llvm bitcode file
		result = lto::parse(member->content(), member->contentSize(), 
								mPath, member->modificationTime(), this->ordinal() + memberIndex, 
								_objOpts.architecture, _objOpts.subType, _logAllFiles);
		if ( result != NULL ) {
			MemberState state = {result, false, false};
			_instantiatedEntries[member] = state;
			return _instantiatedEntries[member];
		}
			
		throwf("archive member '%s' with length %d is not mach-o or llvm bitcode", memberName, member->contentSize());
	}
	catch (const char* msg) {
		throwf("in %s, %s", memberPath, msg);
	}
}


template <typename A>
bool File<A>::forEachAtom(ld::File::AtomHandler& handler) const
{
	bool didSome = false;
	if ( _forceLoadAll || _forceLoadThis ) {
		// call handler on all .o files in this archive
		const Entry* const start = (Entry*)&_archiveFileContent[8];
		const Entry* const end = (Entry*)&_archiveFileContent[_archiveFilelength];
		for (const Entry* p=start; p < end; p = p->next()) {
			const char* memberName = p->name();
			if ( (p==start) && ((strcmp(memberName, SYMDEF_SORTED) == 0) || (strcmp(memberName, SYMDEF) == 0)) )
				continue;
			MemberState& state = this->makeObjectFileForMember(p);
			if ( _verboseLoad ) {
				if ( _forceLoadThis )
					printf("-force_load forced load of %s(%s)\n", this->path(), memberName);
				else
					printf("-all_load forced load of %s(%s)\n", this->path(), memberName);
				state.logged = true;
			}
			didSome |= state.file->forEachAtom(handler);
			state.loaded = true;
		}
	}
	else if ( _forceLoadObjC ) {
		// call handler on all .o files in this archive containing objc classes
		for(typename NameToEntryMap::const_iterator it = _hashTable.begin(); it != _hashTable.end(); ++it) {
			if ( (strncmp(it->first, ".objc_c", 7) == 0) || (strncmp(it->first, "_OBJC_CLASS_$_", 14) == 0) ) {
				const Entry* member = (Entry*)&_archiveFileContent[E::get32(it->second->ran_off)];
				MemberState& state = this->makeObjectFileForMember(member);
				if ( _verboseLoad && !state.logged ) {
					printf("-ObjC forced load of %s(%s)\n", this->path(), member->name());
					state.logged = true;
				}
				if ( ! state.loaded ) {
					didSome |= state.file->forEachAtom(handler);
					state.loaded = true;
				}
			}
		}
		// ObjC2 has no symbols in .o files with categories but not classes, look deeper for those
		const Entry* const start = (Entry*)&_archiveFileContent[8];
		const Entry* const end = (Entry*)&_archiveFileContent[_archiveFilelength];
		for (const Entry* member=start; member < end; member = member->next()) {
			// only look at files not already instantiated
			if ( _instantiatedEntries.count(member) == 0 ) {
				//fprintf(stderr, "checking member %s\n", member->name());
				if ( this->memberHasObjCCategories(member) ) {
					MemberState& state = this->makeObjectFileForMember(member);
					if ( _verboseLoad && !state.logged ) {
						printf("-ObjC forced load of %s(%s)\n", this->path(), member->name());
						state.logged = true;
					}
					if ( ! state.loaded ) {
						didSome |= state.file->forEachAtom(handler);
						state.loaded = true;
					}
				}
			}
		}
	}
	return didSome;
}

template <typename A>
bool File<A>::justInTimeforEachAtom(const char* name, ld::File::AtomHandler& handler) const
{
	// in force load case, all members already loaded
	if ( _forceLoadAll || _forceLoadThis ) 
		return false;
	
	// do a hash search of table of contents looking for requested symbol
	const struct ranlib* result = ranlibHashSearch(name);
	if ( result != NULL ) {
		const Entry* member = (Entry*)&_archiveFileContent[E::get32(result->ran_off)];
		MemberState& state = this->makeObjectFileForMember(member);
		// only call handler for each member once
		if ( ! state.loaded && !state.logged ) {
			if ( _verboseLoad ) {
				printf("%s forced load of %s(%s)\n", name, this->path(), member->name());
				state.logged = true;
			}
			state.loaded = true;
			return state.file->forEachAtom(handler);
		}
	}
	//fprintf(stderr, "%s NOT found in archive %s\n", name, this->path());
	return false;
}

class CheckIsDataSymbolHandler : public ld::File::AtomHandler
{
public:
					CheckIsDataSymbolHandler(const char* n) : _name(n), _isData(false) {}
	virtual void	doAtom(const class ld::Atom& atom) {
						if ( strcmp(atom.name(), _name) == 0 ) {
							if ( atom.section().type() != ld::Section::typeCode )
								_isData = true;
						}
					}
	virtual void	doFile(const class ld::File&) {}
	bool			symbolIsDataDefinition() { return _isData; }

private:
	const char*		_name;
	bool			_isData;

};

template <typename A>
bool File<A>::justInTimeDataOnlyforEachAtom(const char* name, ld::File::AtomHandler& handler) const
{
	// in force load case, all members already loaded
	if ( _forceLoadAll || _forceLoadThis ) 
		return false;
	
	// do a hash search of table of contents looking for requested symbol
	const struct ranlib* result = ranlibHashSearch(name);
	if ( result != NULL ) {
		const Entry* member = (Entry*)&_archiveFileContent[E::get32(result->ran_off)];
		MemberState& state = this->makeObjectFileForMember(member);
		// only call handler for each member once
		if ( ! state.loaded ) {
			CheckIsDataSymbolHandler checker(name);
			state.file->forEachAtom(checker);
			if ( checker.symbolIsDataDefinition() ) {
				if ( _verboseLoad && !state.logged ) {
					printf("%s forced load of %s(%s)\n", name, this->path(), member->name());
					state.logged = true;
				}
				state.loaded = true;
				return state.file->forEachAtom(handler);
			}
		}
	}
	//fprintf(stderr, "%s NOT found in archive %s\n", name, this->path());
	return false;
}


typedef const struct ranlib* ConstRanLibPtr;

template <typename A>
ConstRanLibPtr  File<A>::ranlibHashSearch(const char* name) const
{
	typename NameToEntryMap::const_iterator pos = _hashTable.find(name);
	if ( pos != _hashTable.end() )
		return pos->second;
	else
		return NULL;
}

template <typename A>
void File<A>::buildHashTable()
{
	// walk through list backwards, adding/overwriting entries
	// this assures that with duplicates those earliest in the list will be found
	for (int i = _tableOfContentCount-1; i >= 0; --i) {
		const struct ranlib* entry = &_tableOfContents[i];
		const char* entryName = &_tableOfContentStrings[E::get32(entry->ran_un.ran_strx)];
		if ( E::get32(entry->ran_off) > _archiveFilelength ) {
			throwf("malformed archive TOC entry for %s, offset %d is beyond end of file %lld\n",
				entryName, entry->ran_off, _archiveFilelength);
		}
		
		//const Entry* member = (Entry*)&_archiveFileContent[E::get32(entry->ran_off)];
		//fprintf(stderr, "adding hash %d, %s -> %p\n", i, entryName, entry);
		_hashTable[entryName] = entry;
	}
}

template <typename A>
void File<A>::dumpTableOfContents()
{
	for (unsigned int i=0; i < _tableOfContentCount; ++i) {
		const struct ranlib* e = &_tableOfContents[i];
		printf("%s in %s\n", &_tableOfContentStrings[E::get32(e->ran_un.ran_strx)], ((Entry*)&_archiveFileContent[E::get32(e->ran_off)])->name());
	}
}


//
// main function used by linker to instantiate archive files
//
ld::archive::File* parse(const uint8_t* fileContent, uint64_t fileLength, 
				const char* path, time_t modTime, uint32_t ordinal, const ParserOptions& opts)
{
	switch ( opts.objOpts.architecture ) {
		case CPU_TYPE_X86_64:
			if ( archive::Parser<x86_64>::validFile(fileContent, fileLength, opts.objOpts) )
				return archive::Parser<x86_64>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
		case CPU_TYPE_I386:
			if ( archive::Parser<x86>::validFile(fileContent, fileLength, opts.objOpts) )
				return archive::Parser<x86>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
		case CPU_TYPE_ARM:
			if ( archive::Parser<arm>::validFile(fileContent, fileLength, opts.objOpts) )
				return archive::Parser<arm>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
		case CPU_TYPE_POWERPC:
			if ( archive::Parser<ppc>::validFile(fileContent, fileLength, opts.objOpts) )
				return archive::Parser<ppc>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
		case CPU_TYPE_POWERPC64:
			if ( archive::Parser<ppc64>::validFile(fileContent, fileLength, opts.objOpts) )
				return archive::Parser<ppc64>::parse(fileContent, fileLength, path, modTime, ordinal, opts);
			break;
	}
	return NULL;
}



}; // namespace archive


