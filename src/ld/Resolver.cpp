/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-*
 *
 * Copyright (c) 2009-2010 Apple Inc. All rights reserved.
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
#include <mach-o/fat.h>

#include <string>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <list>
#include <algorithm>
#include <dlfcn.h>
#include <AvailabilityMacros.h>

#include "Options.h"

#include "ld.hpp"
#include "InputFiles.h"
#include "SymbolTable.h"
#include "Resolver.h"
#include "parsers/lto_file.h"


namespace ld {
namespace tool {


//
// An ExportAtom has no content.  It exists so that the linker can track which imported
// symbols came from which dynamic libraries.
//
class UndefinedProxyAtom : public ld::Atom
{
public:
											UndefinedProxyAtom(const char* nm)
												: ld::Atom(_s_section, ld::Atom::definitionProxy, 
													ld::Atom::combineNever, ld::Atom::scopeLinkageUnit, 
													ld::Atom::typeUnclassified, 
													ld::Atom::symbolTableIn, false, false, false, ld::Atom::Alignment(0)), 
													_name(nm) {}
	// overrides of ld::Atom
	virtual const ld::File*						file() const		{ return NULL; }
	virtual const char*							name() const		{ return _name; }
	virtual uint64_t							size() const		{ return 0; }
	virtual uint64_t							objectAddress() const { return 0; }
	virtual void								copyRawContent(uint8_t buffer[]) const { }
	virtual void								setScope(Scope)		{ }

protected:

	virtual									~UndefinedProxyAtom() {}

	const char*								_name;
	
	static ld::Section						_s_section;
};

ld::Section UndefinedProxyAtom::_s_section("__TEXT", "__import", ld::Section::typeImportProxies, true);




class AliasAtom : public ld::Atom
{
public:
										AliasAtom(const ld::Atom& target, const char* nm) : 
											ld::Atom(target.section(), target.definition(), ld::Atom::combineNever,
													ld::Atom::scopeGlobal, target.contentType(), 
													target.symbolTableInclusion(), target.dontDeadStrip(), 
													target.isThumb(), true, target.alignment()),
											_name(nm), 
											_aliasOf(target),
											_fixup(0, ld::Fixup::k1of1, ld::Fixup::kindNoneFollowOn, &target) { }

	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return _aliasOf.file(); }
	virtual const char*						translationUnitSource() const
															{ return _aliasOf.translationUnitSource(); }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return 0; }
	virtual uint64_t					objectAddress() const { return _aliasOf.objectAddress(); }
	virtual void						copyRawContent(uint8_t buffer[]) const { }
	virtual const uint8_t*				rawContentPointer() const { return NULL; }
	virtual unsigned long				contentHash(const class ld::IndirectBindingTable& ibt) const 
															{ return _aliasOf.contentHash(ibt);  }
	virtual bool						canCoalesceWith(const ld::Atom& rhs, const class ld::IndirectBindingTable& ibt) const 
															{ return _aliasOf.canCoalesceWith(rhs,ibt); }
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixup; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return &((ld::Fixup*)&_fixup)[1]; }
	virtual ld::Atom::UnwindInfo::iterator	beginUnwind() const { return  NULL; }
	virtual ld::Atom::UnwindInfo::iterator	endUnwind() const	{ return NULL; }
	virtual ld::Atom::LineInfo::iterator	beginLineInfo() const { return  NULL; }
	virtual ld::Atom::LineInfo::iterator	endLineInfo() const { return NULL; }

	void									setFinalAliasOf() const {
												(const_cast<AliasAtom*>(this))->setAttributesFromAtom(_aliasOf);
												(const_cast<AliasAtom*>(this))->setScope(ld::Atom::scopeGlobal);
											}
															
private:
	const char*							_name;
	const ld::Atom&						_aliasOf;
	ld::Fixup							_fixup;
};



class SectionBoundaryAtom : public ld::Atom
{
public:
	static SectionBoundaryAtom*			makeSectionBoundaryAtom(const char* name, bool start, const char* segSectName); 
	static SectionBoundaryAtom*			makeOldSectionBoundaryAtom(const char* name, bool start);
	
	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return NULL; }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return 0; }
	virtual void						copyRawContent(uint8_t buffer[]) const { }
	virtual const uint8_t*				rawContentPointer() const { return NULL; }
	virtual uint64_t					objectAddress() const { return 0; }
															
private:

										SectionBoundaryAtom(const char* nm, const ld::Section& sect,
															ld::Atom::ContentType cont) : 
											ld::Atom(sect, 
													ld::Atom::definitionRegular, 
													ld::Atom::combineNever,
													ld::Atom::scopeLinkageUnit, 
													cont, 
													ld::Atom::symbolTableNotIn,  
													false, false, true, ld::Atom::Alignment(0)),
											_name(nm) { }

	const char*							_name;
};

SectionBoundaryAtom* SectionBoundaryAtom::makeSectionBoundaryAtom(const char* name, bool start, const char* segSectName)
{
	
	const char* segSectDividor = strrchr(segSectName, '$');
	if ( segSectDividor == NULL )
		throwf("malformed section$ symbol name: %s", name);
	const char* sectionName = segSectDividor + 1;
	int segNameLen = segSectDividor - segSectName;
	if ( segNameLen > 16 )
		throwf("malformed section$ symbol name: %s", name);
	char segName[18];
	strlcpy(segName, segSectName, segNameLen+1);
	
	const ld::Section* section = new ld::Section(strdup(segName), sectionName, ld::Section::typeUnclassified);
	return new SectionBoundaryAtom(name, *section, (start ? ld::Atom::typeSectionStart : typeSectionEnd));
}

SectionBoundaryAtom* SectionBoundaryAtom::makeOldSectionBoundaryAtom(const char* name, bool start)
{
	// e.g. __DATA__bss__begin
	char segName[18];
	strlcpy(segName, name, 7);
	
	char sectName[18];
	int nameLen = strlen(name);
	strlcpy(sectName, &name[6], (start ? nameLen-12 : nameLen-10));
	warning("grandfathering in old symbol '%s' as alias for 'section$%s$%s$%s'", name, start ? "start" : "end", segName, sectName);
	const ld::Section* section = new ld::Section(strdup(segName), strdup(sectName), ld::Section::typeUnclassified);
	return new SectionBoundaryAtom(name, *section, (start ? ld::Atom::typeSectionStart : typeSectionEnd));
}




class SegmentBoundaryAtom : public ld::Atom
{
public:
	static SegmentBoundaryAtom*			makeSegmentBoundaryAtom(const char* name, bool start, const char* segName); 
	static SegmentBoundaryAtom*			makeOldSegmentBoundaryAtom(const char* name, bool start); 
	
	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return NULL; }
	virtual const char*					name() const		{ return _name; }
	virtual uint64_t					size() const		{ return 0; }
	virtual void						copyRawContent(uint8_t buffer[]) const { }
	virtual const uint8_t*				rawContentPointer() const { return NULL; }
	virtual uint64_t					objectAddress() const { return 0; }
															
private:

										SegmentBoundaryAtom(const char* nm, const ld::Section& sect,
															ld::Atom::ContentType cont) : 
											ld::Atom(sect, 
													ld::Atom::definitionRegular, 
													ld::Atom::combineNever,
													ld::Atom::scopeLinkageUnit, 
													cont, 
													ld::Atom::symbolTableNotIn,  
													false, false, true, ld::Atom::Alignment(0)),
											_name(nm) { }

	const char*							_name;
};

SegmentBoundaryAtom* SegmentBoundaryAtom::makeSegmentBoundaryAtom(const char* name, bool start, const char* segName)
{
	if ( *segName == '\0' )
		throwf("malformed segment$ symbol name: %s", name);
	if ( strlen(segName) > 16 )
		throwf("malformed segment$ symbol name: %s", name);
	
	if ( start ) {
		const ld::Section* section = new ld::Section(segName, "__start", ld::Section::typeFirstSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionStart);
	}
	else {
		const ld::Section* section = new ld::Section(segName, "__end", ld::Section::typeLastSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionEnd);
	}
}

SegmentBoundaryAtom* SegmentBoundaryAtom::makeOldSegmentBoundaryAtom(const char* name, bool start)
{
	// e.g. __DATA__begin
	char temp[18];
	strlcpy(temp, name, 7);
	char* segName = strdup(temp);
	
	warning("grandfathering in old symbol '%s' as alias for 'segment$%s$%s'", name, start ? "start" : "end", segName);

	if ( start ) {
		const ld::Section* section = new ld::Section(segName, "__start", ld::Section::typeFirstSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionStart);
	}
	else {
		const ld::Section* section = new ld::Section(segName, "__end", ld::Section::typeLastSection, true);
		return new SegmentBoundaryAtom(name, *section, ld::Atom::typeSectionEnd);
	}
}

void Resolver::initializeState()
{
	// set initial objc constraint based on command line options
	if ( _options.objcGc() )
		_internal.objcObjectConstraint = ld::File::objcConstraintRetainReleaseOrGC;
	else if ( _options.objcGcOnly() )
		_internal.objcObjectConstraint = ld::File::objcConstraintGC;
	
	_internal.cpuSubType = _options.subArchitecture();
	
	// In -r mode, look for -linker_option additions
	if ( _options.outputKind() == Options::kObjectFile ) {
		ld::relocatable::File::LinkerOptionsList lo = _options.linkerOptions();
		for (relocatable::File::LinkerOptionsList::const_iterator it=lo.begin(); it != lo.end(); ++it) {
			doLinkerOption(*it, "command line");
		}
	}
}

void Resolver::buildAtomList()
{
	// each input files contributes initial atoms
	_atoms.reserve(1024);
	_inputFiles.forEachInitialAtom(*this, _internal);
    
	_completedInitialObjectFiles = true;
	
	//_symbolTable.printStatistics();
}


void Resolver::doLinkerOption(const std::vector<const char*>& linkerOption, const char* fileName)
{
	if ( linkerOption.size() == 1 ) {
		const char* lo1 = linkerOption.front();
		if ( strncmp(lo1, "-l", 2) == 0 ) {
			_internal.linkerOptionLibraries.insert(&lo1[2]);
		}
		else {
			warning("unknown linker option from object file ignored: '%s' in %s", lo1, fileName);
		}
	}
	else if ( linkerOption.size() == 2 ) {
		const char* lo2a = linkerOption[0];
		const char* lo2b = linkerOption[1];
		if ( strcmp(lo2a, "-framework") == 0 ) {
			_internal.linkerOptionFrameworks.insert(lo2b);
		}
		else {
			warning("unknown linker option from object file ignored: '%s' '%s' from %s", lo2a, lo2b, fileName);
		}
	}
	else {
		warning("unknown linker option from object file ignored, starting with: '%s' from %s", linkerOption.front(), fileName);
	}
}

void Resolver::doFile(const ld::File& file)
{
	const ld::relocatable::File* objFile = dynamic_cast<const ld::relocatable::File*>(&file);
	const ld::dylib::File* dylibFile = dynamic_cast<const ld::dylib::File*>(&file);

	if ( objFile != NULL ) {
		// if file has linker options, process them
		ld::relocatable::File::LinkerOptionsList* lo = objFile->linkerOptions();
		if ( lo != NULL ) {
			for (relocatable::File::LinkerOptionsList::const_iterator it=lo->begin(); it != lo->end(); ++it) {
				this->doLinkerOption(*it, file.path());
			}
		}
		
		// update which form of ObjC is being used
		switch ( file.objCConstraint() ) {
			case ld::File::objcConstraintNone:
				break;
			case ld::File::objcConstraintRetainRelease:
				if ( _internal.objcObjectConstraint == ld::File::objcConstraintGC )
					throwf("%s built with incompatible Garbage Collection settings to link with previous .o files", file.path());
				if ( _options.objcGcOnly() )
					throwf("command line specified -objc_gc_only, but file is retain/release based: %s", file.path());
				if ( _options.objcGc() )
					throwf("command line specified -objc_gc, but file is retain/release based: %s", file.path());
				if ( !_options.targetIOSSimulator() && (_internal.objcObjectConstraint != ld::File::objcConstraintRetainReleaseForSimulator) )
					_internal.objcObjectConstraint = ld::File::objcConstraintRetainRelease;
				break;
			case ld::File::objcConstraintRetainReleaseOrGC:
				if ( _internal.objcObjectConstraint == ld::File::objcConstraintNone )
					_internal.objcObjectConstraint = ld::File::objcConstraintRetainReleaseOrGC;
				if ( _options.targetIOSSimulator() )
					warning("linking ObjC for iOS Simulator, but object file (%s) was compiled for MacOSX", file.path());
				break;
			case ld::File::objcConstraintGC:
				if ( _internal.objcObjectConstraint == ld::File::objcConstraintRetainRelease )
					throwf("%s built with incompatible Garbage Collection settings to link with previous .o files", file.path());
				_internal.objcObjectConstraint = ld::File::objcConstraintGC;
				if ( _options.targetIOSSimulator() )
					warning("linking ObjC for iOS Simulator, but object file (%s) was compiled for MacOSX", file.path());
				break;
			case ld::File::objcConstraintRetainReleaseForSimulator:
				if ( _internal.objcObjectConstraint == ld::File::objcConstraintNone ) {
					if ( !_options.targetIOSSimulator() && (_options.outputKind() != Options::kObjectFile) )
						warning("ObjC object file (%s) was compiled for iOS Simulator, but linking for MacOSX", file.path());
					_internal.objcObjectConstraint = ld::File::objcConstraintRetainReleaseForSimulator;
				}
				else if ( _internal.objcObjectConstraint != ld::File::objcConstraintRetainReleaseForSimulator ) {
					_internal.objcObjectConstraint = ld::File::objcConstraintRetainReleaseForSimulator;
				}
				break;
		}
	
		// in -r mode, if any .o files have dwarf then add UUID to output .o file
		if ( objFile->debugInfo() == ld::relocatable::File::kDebugInfoDwarf )
			_internal.someObjectFileHasDwarf = true;
			
		// remember if any .o file did not have MH_SUBSECTIONS_VIA_SYMBOLS bit set
		if ( ! objFile->canScatterAtoms() )
			_internal.allObjectFilesScatterable = false;
	
		// update cpu-sub-type
		cpu_subtype_t nextObjectSubType = file.cpuSubType();
		switch ( _options.architecture() ) {
			case CPU_TYPE_ARM:
				if ( _options.subArchitecture() != nextObjectSubType ) {
					if ( (_options.subArchitecture() == CPU_SUBTYPE_ARM_ALL) && _options.forceCpuSubtypeAll() ) {
						// hack to support gcc multillib build that tries to make sub-type-all slice
					}
					else if ( nextObjectSubType == CPU_SUBTYPE_ARM_ALL ) {
						warning("CPU_SUBTYPE_ARM_ALL subtype is deprecated: %s", file.path());
					}
					else if ( _options.allowSubArchitectureMismatches() ) {
						//warning("object file %s was built for different arm sub-type (%d) than link command line (%d)", 
						//	file.path(), nextObjectSubType, _options.subArchitecture());
					}
					else {
						throwf("object file %s was built for different arm sub-type (%d) than link command line (%d)", 
							file.path(), nextObjectSubType, _options.subArchitecture());
					}
				}
				break;
			
			case CPU_TYPE_I386:
				_internal.cpuSubType = CPU_SUBTYPE_I386_ALL;
				break;
				
			case CPU_TYPE_X86_64:
				_internal.cpuSubType = CPU_SUBTYPE_X86_64_ALL;
				break;
		}
	}
	if ( dylibFile != NULL ) {
		// update which form of ObjC dylibs are being linked
		switch ( dylibFile->objCConstraint() ) {
			case ld::File::objcConstraintNone:
				break;
			case ld::File::objcConstraintRetainRelease:
				if ( _internal.objcDylibConstraint == ld::File::objcConstraintGC )
					throwf("%s built with incompatible Garbage Collection settings to link with previous dylibs", file.path());
				if ( _options.objcGcOnly() )
					throwf("command line specified -objc_gc_only, but dylib is retain/release based: %s", file.path());
				if ( _options.objcGc() )
					throwf("command line specified -objc_gc, but dylib is retain/release based: %s", file.path());
				if ( _options.targetIOSSimulator() )
					warning("linking ObjC for iOS Simulator, but dylib (%s) was compiled for MacOSX", file.path());
				_internal.objcDylibConstraint = ld::File::objcConstraintRetainRelease;
				break;
			case ld::File::objcConstraintRetainReleaseOrGC:
				if ( _internal.objcDylibConstraint == ld::File::objcConstraintNone )
					_internal.objcDylibConstraint = ld::File::objcConstraintRetainReleaseOrGC;
				if ( _options.targetIOSSimulator() )
					warning("linking ObjC for iOS Simulator, but dylib (%s) was compiled for MacOSX", file.path());
				break;
			case ld::File::objcConstraintGC:
				if ( _internal.objcDylibConstraint == ld::File::objcConstraintRetainRelease )
					throwf("%s built with incompatible Garbage Collection settings to link with previous dylibs", file.path());
				if ( _options.targetIOSSimulator() )
					warning("linking ObjC for iOS Simulator, but dylib (%s) was compiled for MacOSX", file.path());
 				_internal.objcDylibConstraint = ld::File::objcConstraintGC;
				break;
			case ld::File::objcConstraintRetainReleaseForSimulator:
				if ( _internal.objcDylibConstraint == ld::File::objcConstraintNone )
					_internal.objcDylibConstraint = ld::File::objcConstraintRetainReleaseForSimulator;
				else if ( _internal.objcDylibConstraint != ld::File::objcConstraintRetainReleaseForSimulator ) {
					warning("ObjC dylib (%s) was compiled for iOS Simulator, but dylibs others were compiled for MacOSX", file.path());
					_internal.objcDylibConstraint = ld::File::objcConstraintRetainReleaseForSimulator;
				}
				break;
		}
	}

}

void Resolver::doAtom(const ld::Atom& atom)
{
	//fprintf(stderr, "Resolver::doAtom(%p), name=%s, sect=%s\n", &atom, atom.name(), atom.section().sectionName());

	// add to list of known atoms
	_atoms.push_back(&atom);
	
	// adjust scope
	if ( _options.hasExportRestrictList() || _options.hasReExportList() ) {
		const char* name = atom.name();
		switch ( atom.scope() ) {
			case ld::Atom::scopeTranslationUnit:
				break;
			case ld::Atom::scopeLinkageUnit:
				if ( _options.hasExportMaskList() && _options.shouldExport(name) ) {
					// <rdar://problem/5062685> ld does not report error when -r is used and exported symbols are not defined.
					if ( _options.outputKind() == Options::kObjectFile ) 
						throwf("cannot export hidden symbol %s", name);
					// .objc_class_name_* symbols are special 
					if ( atom.section().type() != ld::Section::typeObjC1Classes ) {
						if ( atom.definition() == ld::Atom::definitionProxy ) {
							// .exp file says to export a symbol, but that symbol is in some dylib being linked
							if ( _options.canReExportSymbols() ) {
								// marking proxy atom as global triggers the re-export
								(const_cast<ld::Atom*>(&atom))->setScope(ld::Atom::scopeGlobal);
							}
							else if ( _options.outputKind() == Options::kDynamicLibrary ) {
								if ( atom.file() != NULL )
									warning("target OS does not support re-exporting symbol %s from %s\n", _options.demangleSymbol(name), atom.file()->path());
								else
									warning("target OS does not support re-exporting symbol %s\n", _options.demangleSymbol(name));
							}
						}
						else {
							if ( atom.file() != NULL )
								warning("cannot export hidden symbol %s from %s", _options.demangleSymbol(name), atom.file()->path());
							else
								warning("cannot export hidden symbol %s", _options.demangleSymbol(name));
						}
					}
				}
				else if ( _options.shouldReExport(name) && _options.canReExportSymbols() ) {
					if ( atom.definition() == ld::Atom::definitionProxy ) {
						// marking proxy atom as global triggers the re-export
						(const_cast<ld::Atom*>(&atom))->setScope(ld::Atom::scopeGlobal);
					}
					else {
						throwf("requested re-export symbol %s is not from a dylib, but from %s\n", _options.demangleSymbol(name), atom.file()->path());
					}
				}
				break;
			case ld::Atom::scopeGlobal:
				// check for globals that are downgraded to hidden
				if ( ! _options.shouldExport(name) ) {
					(const_cast<ld::Atom*>(&atom))->setScope(ld::Atom::scopeLinkageUnit);
					//fprintf(stderr, "demote %s to hidden\n", name);
				}
				if ( _options.canReExportSymbols() && _options.shouldReExport(name) ) {
					throwf("requested re-export symbol %s is not from a dylib, but from %s\n", _options.demangleSymbol(name), atom.file()->path());
				}
				break;
		}
	}

	// work around for kernel that uses 'l' labels in assembly code
	if ( (atom.symbolTableInclusion() == ld::Atom::symbolTableNotInFinalLinkedImages) 
			&& (atom.name()[0] == 'l') && (_options.outputKind() == Options::kStaticExecutable) 
			&& (strncmp(atom.name(), "ltmp", 4) != 0) )
		(const_cast<ld::Atom*>(&atom))->setSymbolTableInclusion(ld::Atom::symbolTableIn);


	// tell symbol table about non-static atoms
	if ( atom.scope() != ld::Atom::scopeTranslationUnit ) {
		_symbolTable.add(atom, _options.deadCodeStrip() && _completedInitialObjectFiles);
		
		// add symbol aliases defined on the command line
		if ( _options.haveCmdLineAliases() ) {
			const std::vector<Options::AliasPair>& aliases = _options.cmdLineAliases();
			for (std::vector<Options::AliasPair>::const_iterator it=aliases.begin(); it != aliases.end(); ++it) {
				if ( strcmp(it->realName, atom.name()) == 0 ) {
					const AliasAtom* alias = new AliasAtom(atom, it->alias);
					_aliasesFromCmdLine.push_back(alias);
					this->doAtom(*alias);
				}
			}
		}
	}

	// convert references by-name or by-content to by-slot
	this->convertReferencesToIndirect(atom);
	
	// remember if any atoms are proxies that require LTO
	if ( atom.contentType() == ld::Atom::typeLTOtemporary )
		_haveLLVMObjs = true;
		
	if ( _options.deadCodeStrip() ) {
		// add to set of dead-strip-roots, all symbols that the compiler marks as don't strip
		if ( atom.dontDeadStrip() )
			_deadStripRoots.insert(&atom);
			
		if ( atom.scope() == ld::Atom::scopeGlobal ) {
			// <rdar://problem/5524973> -exported_symbols_list that has wildcards and -dead_strip
			// in dylibs, every global atom in initial .o files is a root
			if ( _options.hasWildCardExportRestrictList() || _options.allGlobalsAreDeadStripRoots() ) {
				if ( _options.shouldExport(atom.name()) )
					_deadStripRoots.insert(&atom);
			}
		}
	}
}

bool Resolver::isDtraceProbe(ld::Fixup::Kind kind)
{
	switch (kind) {
		case ld::Fixup::kindStoreX86DtraceCallSiteNop:
		case ld::Fixup::kindStoreX86DtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreARMDtraceCallSiteNop:
		case ld::Fixup::kindStoreARMDtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreARM64DtraceCallSiteNop:
		case ld::Fixup::kindStoreARM64DtraceIsEnableSiteClear:
		case ld::Fixup::kindStoreThumbDtraceCallSiteNop:
		case ld::Fixup::kindStoreThumbDtraceIsEnableSiteClear:
		case ld::Fixup::kindDtraceExtra:
			return true;
		default: 
			break;
	}
	return false;
}

void Resolver::convertReferencesToIndirect(const ld::Atom& atom)
{
	// convert references by-name or by-content to by-slot
	SymbolTable::IndirectBindingSlot slot;
	const ld::Atom* dummy;
	ld::Fixup::iterator end = atom.fixupsEnd();
	for (ld::Fixup::iterator fit=atom.fixupsBegin(); fit != end; ++fit) {
		switch ( fit->binding ) { 
			case ld::Fixup::bindingByNameUnbound:
				if ( isDtraceProbe(fit->kind) && (_options.outputKind() != Options::kObjectFile ) ) {
					// in final linked images, remove reference
					fit->binding = ld::Fixup::bindingNone;
				}
				else {
					slot = _symbolTable.findSlotForName(fit->u.name);
					fit->binding = ld::Fixup::bindingsIndirectlyBound;
					fit->u.bindingIndex = slot;
				}
				break;
			case ld::Fixup::bindingByContentBound:
				switch ( fit->u.target->combine() ) {
					case ld::Atom::combineNever:
					case ld::Atom::combineByName:
						assert(0 && "wrong combine type for bind by content");
						break;
					case ld::Atom::combineByNameAndContent:
						slot = _symbolTable.findSlotForContent(fit->u.target, &dummy);
						fit->binding = ld::Fixup::bindingsIndirectlyBound;
						fit->u.bindingIndex = slot;
						break;
					case ld::Atom::combineByNameAndReferences:
						slot = _symbolTable.findSlotForReferences(fit->u.target, &dummy);
						fit->binding = ld::Fixup::bindingsIndirectlyBound;
						fit->u.bindingIndex = slot;
						break;
				}
				break;
			case ld::Fixup::bindingNone:
			case ld::Fixup::bindingDirectlyBound:
			case ld::Fixup::bindingsIndirectlyBound:
				break;
		}
	}
}


void Resolver::addInitialUndefines()
{
	// add initial undefines from -u option
	for (Options::UndefinesIterator it=_options.initialUndefinesBegin(); it != _options.initialUndefinesEnd(); ++it) {
		_symbolTable.findSlotForName(*it);
	}
}

void Resolver::resolveUndefines()
{
	// keep looping until no more undefines were added in last loop
	unsigned int undefineGenCount = 0xFFFFFFFF;
	while ( undefineGenCount != _symbolTable.updateCount() ) {
		undefineGenCount = _symbolTable.updateCount();
		std::vector<const char*> undefineNames;
		_symbolTable.undefines(undefineNames);
		for(std::vector<const char*>::iterator it = undefineNames.begin(); it != undefineNames.end(); ++it) {
			const char* undef = *it;
			// load for previous undefine may also have loaded this undefine, so check again
			if ( ! _symbolTable.hasName(undef) ) {
				_inputFiles.searchLibraries(undef, true, true, false, *this);
				if ( !_symbolTable.hasName(undef) && (_options.outputKind() != Options::kObjectFile) ) {
					if ( strncmp(undef, "section$", 8) == 0 ) {
						if ( strncmp(undef, "section$start$", 14) == 0 ) {
							this->doAtom(*SectionBoundaryAtom::makeSectionBoundaryAtom(undef, true, &undef[14])); 
						}
						else if ( strncmp(undef, "section$end$", 12) == 0 ) {
							this->doAtom(*SectionBoundaryAtom::makeSectionBoundaryAtom(undef, false, &undef[12])); 
						}
					}
					else if ( strncmp(undef, "segment$", 8) == 0 ) {
						if ( strncmp(undef, "segment$start$", 14) == 0 ) {
							this->doAtom(*SegmentBoundaryAtom::makeSegmentBoundaryAtom(undef, true, &undef[14])); 
						}
						else if ( strncmp(undef, "segment$end$", 12) == 0 ) {
							this->doAtom(*SegmentBoundaryAtom::makeSegmentBoundaryAtom(undef, false, &undef[12])); 
						}
					}
					else if ( _options.outputKind() == Options::kPreload ) {
						// for iBoot grandfather in old style section labels
						int undefLen = strlen(undef);
						if ( strcmp(&undef[undefLen-7], "__begin") == 0 ) {
							if ( undefLen > 13 )
								this->doAtom(*SectionBoundaryAtom::makeOldSectionBoundaryAtom(undef, true));
							else
								this->doAtom(*SegmentBoundaryAtom::makeOldSegmentBoundaryAtom(undef, true));
						}
						else if ( strcmp(&undef[undefLen-5], "__end") == 0 ) {
							if ( undefLen > 11 )
								this->doAtom(*SectionBoundaryAtom::makeOldSectionBoundaryAtom(undef, false));
							else
								this->doAtom(*SegmentBoundaryAtom::makeOldSegmentBoundaryAtom(undef, false));
						}
					}
				}
			}
		}
		// <rdar://problem/5894163> need to search archives for overrides of common symbols 
		if ( _symbolTable.hasExternalTentativeDefinitions() ) {
			bool searchDylibs = (_options.commonsMode() == Options::kCommonsOverriddenByDylibs);
			std::vector<const char*> tents;
			_symbolTable.tentativeDefs(tents);
			for(std::vector<const char*>::iterator it = tents.begin(); it != tents.end(); ++it) {
				// load for previous tentative may also have loaded this tentative, so check again
				const ld::Atom* curAtom = _symbolTable.atomForSlot(_symbolTable.findSlotForName(*it));
				assert(curAtom != NULL);
				if ( curAtom->definition() == ld::Atom::definitionTentative ) {
					_inputFiles.searchLibraries(*it, searchDylibs, true, true, *this);
				}
			}
		}
	}
	
	// Use linker options to resolve an remaining undefined symbols
	if ( !_internal.linkerOptionLibraries.empty() || !_internal.linkerOptionFrameworks.empty() ) {
		std::vector<const char*> undefineNames;
		_symbolTable.undefines(undefineNames);
		if ( undefineNames.size() != 0 ) {
			for (std::vector<const char*>::iterator it = undefineNames.begin(); it != undefineNames.end(); ++it) {
				const char* undef = *it;
				if ( ! _symbolTable.hasName(undef) ) {
					_inputFiles.searchLibraries(undef, true, true, false, *this);
				}
			}
		}
	}
	
	// create proxies as needed for undefined symbols
	if ( (_options.undefinedTreatment() != Options::kUndefinedError) || (_options.outputKind() == Options::kObjectFile) ) {
		std::vector<const char*> undefineNames;
		_symbolTable.undefines(undefineNames);
		for(std::vector<const char*>::iterator it = undefineNames.begin(); it != undefineNames.end(); ++it) {
			// make proxy
			this->doAtom(*new UndefinedProxyAtom(*it));
		}
	}
	
	// support -U option
	if ( _options.someAllowedUndefines() ) {
		std::vector<const char*> undefineNames;
		_symbolTable.undefines(undefineNames);
		for(std::vector<const char*>::iterator it = undefineNames.begin(); it != undefineNames.end(); ++it) {
			if ( _options.allowedUndefined(*it) ) {
				// make proxy
				this->doAtom(*new UndefinedProxyAtom(*it));
			}
		}
	}
	
}


void Resolver::markLive(const ld::Atom& atom, WhyLiveBackChain* previous)
{
	//fprintf(stderr, "markLive(%p) %s\n", &atom, atom.name());
	// if -why_live cares about this symbol, then dump chain
	if ( (previous->referer != NULL) && _options.printWhyLive(atom.name()) ) {
		fprintf(stderr, "%s from %s\n", atom.name(), atom.file()->path());
		int depth = 1;
		for(WhyLiveBackChain* p = previous; p != NULL; p = p->previous, ++depth) {
			for(int i=depth; i > 0; --i)
				fprintf(stderr, "  ");
			fprintf(stderr, "%s from %s\n", p->referer->name(), p->referer->file()->path());
		}
	}
	
	// if already marked live, then done (stop recursion)
	if ( atom.live() )
		return;
		
	// mark this atom is live
	(const_cast<ld::Atom*>(&atom))->setLive();
	
	// mark all atoms it references as live
	WhyLiveBackChain thisChain;
	thisChain.previous = previous;
	thisChain.referer = &atom;
	for (ld::Fixup::iterator fit = atom.fixupsBegin(), end=atom.fixupsEnd(); fit != end; ++fit) {
		const ld::Atom* target;
		switch ( fit->kind ) {
			case ld::Fixup::kindNone:
			case ld::Fixup::kindNoneFollowOn:
			case ld::Fixup::kindNoneGroupSubordinate:
			case ld::Fixup::kindNoneGroupSubordinateFDE:
			case ld::Fixup::kindNoneGroupSubordinateLSDA:
			case ld::Fixup::kindNoneGroupSubordinatePersonality:
			case ld::Fixup::kindSetTargetAddress:
			case ld::Fixup::kindSubtractTargetAddress:
			case ld::Fixup::kindStoreTargetAddressLittleEndian32:
			case ld::Fixup::kindStoreTargetAddressLittleEndian64:
			case ld::Fixup::kindStoreTargetAddressBigEndian32:
			case ld::Fixup::kindStoreTargetAddressBigEndian64:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32:
			case ld::Fixup::kindStoreTargetAddressX86BranchPCRel32:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoad:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32GOTLoadNowLEA:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoad:
			case ld::Fixup::kindStoreTargetAddressX86PCRel32TLVLoadNowLEA:
			case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoad:
			case ld::Fixup::kindStoreTargetAddressX86Abs32TLVLoadNowLEA:
			case ld::Fixup::kindStoreTargetAddressARMBranch24:
			case ld::Fixup::kindStoreTargetAddressThumbBranch22:
#if SUPPORT_ARCH_arm64
			case ld::Fixup::kindStoreTargetAddressARM64Branch26:
			case ld::Fixup::kindStoreTargetAddressARM64Page21:
			case ld::Fixup::kindStoreTargetAddressARM64GOTLoadPage21:
			case ld::Fixup::kindStoreTargetAddressARM64GOTLeaPage21:
#endif
				if ( fit->binding == ld::Fixup::bindingByContentBound ) {
					// normally this was done in convertReferencesToIndirect()
					// but a archive loaded .o file may have a forward reference
					SymbolTable::IndirectBindingSlot slot;
					const ld::Atom* dummy;
					switch ( fit->u.target->combine() ) {
						case ld::Atom::combineNever:
						case ld::Atom::combineByName:
							assert(0 && "wrong combine type for bind by content");
							break;
						case ld::Atom::combineByNameAndContent:
							slot = _symbolTable.findSlotForContent(fit->u.target, &dummy);
							fit->binding = ld::Fixup::bindingsIndirectlyBound;
							fit->u.bindingIndex = slot;
							break;
						case ld::Atom::combineByNameAndReferences:
							slot = _symbolTable.findSlotForReferences(fit->u.target, &dummy);
							fit->binding = ld::Fixup::bindingsIndirectlyBound;
							fit->u.bindingIndex = slot;
							break;
					}
				}
				switch ( fit->binding ) {
					case ld::Fixup::bindingDirectlyBound:
						markLive(*(fit->u.target), &thisChain);
						break;
					case ld::Fixup::bindingByNameUnbound:
						// doAtom() did not convert to indirect in dead-strip mode, so that now
						fit->u.bindingIndex = _symbolTable.findSlotForName(fit->u.name);
						fit->binding = ld::Fixup::bindingsIndirectlyBound;
						// fall into next case
					case ld::Fixup::bindingsIndirectlyBound:
						target = _internal.indirectBindingTable[fit->u.bindingIndex];
						if ( target == NULL ) {
							const char* targetName = _symbolTable.indirectName(fit->u.bindingIndex);
							_inputFiles.searchLibraries(targetName, true, true, false, *this);
							target = _internal.indirectBindingTable[fit->u.bindingIndex];
						}
						if ( target != NULL ) {
							if ( target->definition() == ld::Atom::definitionTentative ) {
								// <rdar://problem/5894163> need to search archives for overrides of common symbols 
								bool searchDylibs = (_options.commonsMode() == Options::kCommonsOverriddenByDylibs);
								_inputFiles.searchLibraries(target->name(), searchDylibs, true, true, *this);
								// recompute target since it may have been overridden by searchLibraries()
								target = _internal.indirectBindingTable[fit->u.bindingIndex];
							}
							this->markLive(*target, &thisChain);
						}
						else {
							_atomsWithUnresolvedReferences.push_back(&atom);
						}
						break;
					default:
						assert(0 && "bad binding during dead stripping");
				}
				break;
            default:
                break;    
		}
	}

}

class NotLiveLTO {
public:
	bool operator()(const ld::Atom* atom) const {
		if (atom->live() || atom->dontDeadStrip() )
			return false;
		// don't kill combinable atoms in first pass
		switch ( atom->combine() ) {
			case ld::Atom::combineByNameAndContent:
			case ld::Atom::combineByNameAndReferences:
				return false;
			default:
				return true;
		}
	}
};

void Resolver::deadStripOptimize(bool force)
{
	// only do this optimization with -dead_strip
	if ( ! _options.deadCodeStrip() ) 
		return;
		
	// add entry point (main) to live roots
	const ld::Atom* entry = this->entryPoint(true);
	if ( entry != NULL )
		_deadStripRoots.insert(entry);
		
	// add -exported_symbols_list, -init, and -u entries to live roots
	for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
		SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName(*uit);
		if ( _internal.indirectBindingTable[slot] == NULL ) {
			_inputFiles.searchLibraries(*uit, false, true, false, *this);
		}
		if ( _internal.indirectBindingTable[slot] != NULL )
			_deadStripRoots.insert(_internal.indirectBindingTable[slot]);
	}
	
	// this helper is only referenced by synthesize stubs, assume it will be used
	if ( _internal.classicBindingHelper != NULL ) 
		_deadStripRoots.insert(_internal.classicBindingHelper);

	// this helper is only referenced by synthesize stubs, assume it will be used
	if ( _internal.compressedFastBinderProxy != NULL ) 
		_deadStripRoots.insert(_internal.compressedFastBinderProxy);

	// this helper is only referenced by synthesized lazy stubs, assume it will be used
	if ( _internal.lazyBindingHelper != NULL )
		_deadStripRoots.insert(_internal.lazyBindingHelper);

	// add all dont-dead-strip atoms as roots
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		if ( atom->dontDeadStrip() ) {
			//fprintf(stderr, "dont dead strip: %p %s %s\n", atom, atom->section().sectionName(), atom->name());
			_deadStripRoots.insert(atom);
			// unset liveness, so markLive() will recurse
			(const_cast<ld::Atom*>(atom))->setLive(0);
		}
	}
	
	// mark all roots as live, and all atoms they reference
	for (std::set<const ld::Atom*>::iterator it=_deadStripRoots.begin(); it != _deadStripRoots.end(); ++it) {
		WhyLiveBackChain rootChain;
		rootChain.previous = NULL;
		rootChain.referer = *it;
		this->markLive(**it, &rootChain);
	}
	
	// now remove all non-live atoms from _atoms
	const bool log = false;
	if ( log ) {
		fprintf(stderr, "deadStripOptimize() all %ld atoms with liveness:\n", _atoms.size());
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			const ld::File* file = (*it)->file();
			fprintf(stderr, "  live=%d  atom=%p  name=%s from=%s\n", (*it)->live(), *it, (*it)->name(),  (file ? file->path() : "<internal>"));
		}
	}
	
	if ( _haveLLVMObjs && !force ) {
		// <rdar://problem/9777977> don't remove combinable atoms, they may come back in lto output
		_atoms.erase(std::remove_if(_atoms.begin(), _atoms.end(), NotLiveLTO()), _atoms.end());
	}
	else {
		_atoms.erase(std::remove_if(_atoms.begin(), _atoms.end(), NotLive()), _atoms.end());
	}

	if ( log ) {
		fprintf(stderr, "deadStripOptimize() %ld remaining atoms\n", _atoms.size());
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			fprintf(stderr, "  live=%d  atom=%p  name=%s\n", (*it)->live(), *it, (*it)->name());
		}
	}
}


// This is called when LTO is used but -dead_strip is not used.
// Some undefines were eliminated by LTO, but others were not.
void Resolver::remainingUndefines(std::vector<const char*>& undefs)
{
	StringSet  undefSet;
	// search all atoms for references that are unbound
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
			switch ( (ld::Fixup::TargetBinding)fit->binding ) {
				case ld::Fixup::bindingByNameUnbound:
					assert(0 && "should not be by-name this late");
					undefSet.insert(fit->u.name);
					break;
				case ld::Fixup::bindingsIndirectlyBound:
					if ( _internal.indirectBindingTable[fit->u.bindingIndex] == NULL ) {
						undefSet.insert(_symbolTable.indirectName(fit->u.bindingIndex));
					}
					break;
				case ld::Fixup::bindingByContentBound:
				case ld::Fixup::bindingNone:
				case ld::Fixup::bindingDirectlyBound:
					break;
			}
		}
	}
	// look for any initial undefines that are still undefined
	for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
		if ( ! _symbolTable.hasName(*uit) ) {
			undefSet.insert(*uit);
		}
	}
	
	// copy set to vector
	for (StringSet::const_iterator it=undefSet.begin(); it != undefSet.end(); ++it) {
        fprintf(stderr, "undef: %s\n", *it);
		undefs.push_back(*it);
	}
}

void Resolver::liveUndefines(std::vector<const char*>& undefs)
{
	StringSet  undefSet;
	// search all live atoms for references that are unbound
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		if ( ! atom->live() )
			continue;
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
			switch ( (ld::Fixup::TargetBinding)fit->binding ) {
				case ld::Fixup::bindingByNameUnbound:
					assert(0 && "should not be by-name this late");
					undefSet.insert(fit->u.name);
					break;
				case ld::Fixup::bindingsIndirectlyBound:
					if ( _internal.indirectBindingTable[fit->u.bindingIndex] == NULL ) {
						undefSet.insert(_symbolTable.indirectName(fit->u.bindingIndex));
					}
					break;
				case ld::Fixup::bindingByContentBound:
				case ld::Fixup::bindingNone:
				case ld::Fixup::bindingDirectlyBound:
					break;
			}
		}
	}
	// look for any initial undefines that are still undefined
	for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
		if ( ! _symbolTable.hasName(*uit) ) {
			undefSet.insert(*uit);
		}
	}
	
	// copy set to vector
	for (StringSet::const_iterator it=undefSet.begin(); it != undefSet.end(); ++it) {
		undefs.push_back(*it);
	}
}



// <rdar://problem/8252819> warn when .objc_class_name_* symbol missing
class ExportedObjcClass
{
public:
	ExportedObjcClass(const Options& opt) : _options(opt)  {}

	bool operator()(const char* name) const {
		if ( (strncmp(name, ".objc_class_name_", 17) == 0) && _options.shouldExport(name) ) {
			warning("ignoring undefined symbol %s from -exported_symbols_list", name);
			return true;
		}
		const char* s = strstr(name, "CLASS_$_");
		if ( s != NULL ) {
			char temp[strlen(name)+16];
			strcpy(temp, ".objc_class_name_");
			strcat(temp, &s[8]);
			if ( _options.wasRemovedExport(temp) ) {
				warning("ignoring undefined symbol %s from -exported_symbols_list", temp);
				return true;
			}
		}
		return false;
	}
private:
	const Options& _options;
};


// temp hack for undefined aliases
class UndefinedAlias
{
public:
	UndefinedAlias(const Options& opt) : _aliases(opt.cmdLineAliases()) {}

	bool operator()(const char* name) const {
		for (std::vector<Options::AliasPair>::const_iterator it=_aliases.begin(); it != _aliases.end(); ++it) {
			if ( strcmp(it->realName, name) == 0 ) {
				warning("undefined base symbol '%s' for alias '%s'", name, it->alias);
				return true;
			}
		}
		return false;
	}
private:
	const std::vector<Options::AliasPair>&	_aliases;
};



static const char* pathLeafName(const char* path)
{
	const char* shortPath = strrchr(path, '/');
	if ( shortPath == NULL )
		return path;
	else
		return &shortPath[1];
}

bool Resolver::printReferencedBy(const char* name, SymbolTable::IndirectBindingSlot slot)
{
	unsigned foundReferenceCount = 0;
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
		const ld::Atom* atom = *it;
		for (ld::Fixup::iterator fit=atom->fixupsBegin(); fit != atom->fixupsEnd(); ++fit) {
			if ( fit->binding == ld::Fixup::bindingsIndirectlyBound ) {
				if ( fit->u.bindingIndex == slot ) {
					if ( atom->contentType() == ld::Atom::typeNonLazyPointer ) {
						const ld::Atom* existingAtom;
						unsigned int nlSlot = _symbolTable.findSlotForReferences(atom, &existingAtom);
						if ( printReferencedBy(name, nlSlot) )
							++foundReferenceCount;
					}
					else if ( atom->contentType() == ld::Atom::typeCFI ) {
						fprintf(stderr, "      Dwarf Exception Unwind Info (__eh_frame) in %s\n", pathLeafName(atom->file()->path()));
						++foundReferenceCount;
					}
					else {
						fprintf(stderr, "      %s in %s\n", _options.demangleSymbol(atom->name()), pathLeafName(atom->file()->path()));
						++foundReferenceCount;
						break; // if undefined used twice in a function, only show first
					}
				}
			}
		}
		if ( foundReferenceCount > 6 ) {
			fprintf(stderr, "      ...\n");
			break; // only show first six uses of undefined symbol
		}
	}
	return (foundReferenceCount != 0);
}

void Resolver::checkUndefines(bool force)
{
	// when using LTO, undefines are checked after bitcode is optimized
	if ( _haveLLVMObjs && !force )
		return;

	// error out on any remaining undefines
	bool doPrint = true;
	bool doError = true;
	switch ( _options.undefinedTreatment() ) {
		case Options::kUndefinedError:
			break;
		case Options::kUndefinedDynamicLookup:
			doError = false;
			break;
		case Options::kUndefinedWarning:
			doError = false;
			break;
		case Options::kUndefinedSuppress:
			doError = false;
			doPrint = false;
			break;
	}
	std::vector<const char*> unresolvableUndefines;
	if ( _options.deadCodeStrip() )
		this->liveUndefines(unresolvableUndefines);
    else if( _haveLLVMObjs ) 
		this->remainingUndefines(unresolvableUndefines); // <rdar://problem/10052396> LTO may have eliminated need for some undefines
	else	
		_symbolTable.undefines(unresolvableUndefines);

	// <rdar://problem/8252819> assert when .objc_class_name_* symbol missing
	if ( _options.hasExportMaskList() ) {
		unresolvableUndefines.erase(std::remove_if(unresolvableUndefines.begin(), unresolvableUndefines.end(), ExportedObjcClass(_options)), unresolvableUndefines.end());
	}

	// hack to temporarily make missing aliases a warning
	if ( _options.haveCmdLineAliases() ) {
		unresolvableUndefines.erase(std::remove_if(unresolvableUndefines.begin(), unresolvableUndefines.end(), UndefinedAlias(_options)), unresolvableUndefines.end());
	}
	
	const int unresolvableCount = unresolvableUndefines.size();
	int unresolvableExportsCount = 0;
	if ( unresolvableCount != 0 ) {
		if ( doPrint ) {
			if ( _options.printArchPrefix() )
				fprintf(stderr, "Undefined symbols for architecture %s:\n", _options.architectureName());
			else
				fprintf(stderr, "Undefined symbols:\n");
			for (int i=0; i < unresolvableCount; ++i) {
				const char* name = unresolvableUndefines[i];
				unsigned int slot = _symbolTable.findSlotForName(name);
				fprintf(stderr, "  \"%s\", referenced from:\n", _options.demangleSymbol(name));
				// scan all atoms for references
				bool foundAtomReference = printReferencedBy(name, slot);
				// scan command line options
				if  ( !foundAtomReference ) {
					// might be from -init command line option
					if ( (_options.initFunctionName() != NULL) && (strcmp(name, _options.initFunctionName()) == 0) ) {
						fprintf(stderr, "     -init command line option\n");
					}
					// or might be from exported symbol option
					else if ( _options.hasExportMaskList() && _options.shouldExport(name) ) {
						fprintf(stderr, "     -exported_symbol[s_list] command line option\n");
					}
					// or might be from re-exported symbol option
					else if ( _options.hasReExportList() && _options.shouldReExport(name) ) {
						fprintf(stderr, "     -reexported_symbols_list command line option\n");
					}
					else if ( (_options.outputKind() == Options::kDynamicExecutable)
							&& (_options.entryName() != NULL) && (strcmp(name, _options.entryName()) == 0) ) {
						fprintf(stderr, "     implicit entry/start for main executable\n");
					}
					else {
						bool isInitialUndefine = false;
						for (Options::UndefinesIterator uit=_options.initialUndefinesBegin(); uit != _options.initialUndefinesEnd(); ++uit) {
							if ( strcmp(*uit, name) == 0 ) {
								isInitialUndefine = true;
								break;
							}
						}
						if ( isInitialUndefine )
							fprintf(stderr, "     -u command line option\n");
					}
					++unresolvableExportsCount;
				}
				// be helpful and check for typos
				bool printedStart = false;
				for (SymbolTable::byNameIterator sit=_symbolTable.begin(); sit != _symbolTable.end(); sit++) {
					const ld::Atom* atom = *sit;
					if ( (atom != NULL) && (atom->symbolTableInclusion() == ld::Atom::symbolTableIn) && (strstr(atom->name(), name) != NULL) ) {
						if ( ! printedStart ) {
							fprintf(stderr, "     (maybe you meant: %s", atom->name());
							printedStart = true;
						}
						else {
							fprintf(stderr, ", %s ", atom->name());
						}
					}
				}
				if ( printedStart )
					fprintf(stderr, ")\n");
				// <rdar://problem/8989530> Add comment to error message when __ZTV symbols are undefined
				if ( strncmp(name, "__ZTV", 5) == 0 ) {
					fprintf(stderr, "  NOTE: a missing vtable usually means the first non-inline virtual member function has no definition.\n");
				}
			}
		}
		if ( doError ) 
			throw "symbol(s) not found";
	}
	
}



void Resolver::checkDylibSymbolCollisions()
{	
	for (SymbolTable::byNameIterator it=_symbolTable.begin(); it != _symbolTable.end(); it++) {
		const ld::Atom* atom = *it;
		if ( atom == NULL )
			continue;
		if ( atom->scope() == ld::Atom::scopeGlobal ) {
			// <rdar://problem/5048861> No warning about tentative definition conflicting with dylib definition
			// for each tentative definition in symbol table look for dylib that exports same symbol name
			if ( atom->definition() == ld::Atom::definitionTentative ) {
				_inputFiles.searchLibraries(atom->name(), true, false, false, *this);
			}
			// record any overrides of weak symbols in any linked dylib 
			if ( (atom->definition() == ld::Atom::definitionRegular) && (atom->symbolTableInclusion() == ld::Atom::symbolTableIn) ) {
				if ( _inputFiles.searchWeakDefInDylib(atom->name()) )
					(const_cast<ld::Atom*>(atom))->setOverridesDylibsWeakDef();
			}
		}
	}
}	


const ld::Atom* Resolver::entryPoint(bool searchArchives)
{
	const char* symbolName = NULL;
	bool makingDylib = false;
	switch ( _options.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kStaticExecutable:
		case Options::kDyld:
		case Options::kPreload:
			symbolName = _options.entryName();
			break;
		case Options::kDynamicLibrary:
			symbolName = _options.initFunctionName();
			makingDylib = true;
			break;
		case Options::kObjectFile:
		case Options::kDynamicBundle:
		case Options::kKextBundle:
			return NULL;
			break;
	}
	if ( symbolName != NULL ) {
		SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName(symbolName);
		if ( (_internal.indirectBindingTable[slot] == NULL) && searchArchives ) {
			// <rdar://problem/7043256> ld64 can not find a -e entry point from an archive				
			_inputFiles.searchLibraries(symbolName, false, true, false, *this);
		}
		if ( _internal.indirectBindingTable[slot] == NULL ) {
			if ( strcmp(symbolName, "start") == 0 )
				throwf("entry point (%s) undefined.  Usually in crt1.o", symbolName);
			else
				throwf("entry point (%s) undefined.", symbolName);
		}
		else if ( _internal.indirectBindingTable[slot]->definition() == ld::Atom::definitionProxy ) {
			if ( makingDylib ) 
				throwf("-init function (%s) found in linked dylib, must be in dylib being linked", symbolName);
		}
		return _internal.indirectBindingTable[slot];
	}
	return NULL;
}


void Resolver::fillInHelpersInInternalState()
{
	// look up well known atoms
	bool needsStubHelper = true;
	switch ( _options.outputKind() ) {
		case Options::kDynamicExecutable:
		case Options::kDynamicLibrary:
		case Options::kDynamicBundle:
			needsStubHelper = true;
			break;
		case Options::kDyld:
		case Options::kKextBundle:
		case Options::kObjectFile:
		case Options::kStaticExecutable:
		case Options::kPreload:
			needsStubHelper = false;
			break;
	}
	
	_internal.classicBindingHelper = NULL;
	if ( needsStubHelper && !_options.makeCompressedDyldInfo() ) { 
		// "dyld_stub_binding_helper" comes from .o file, so should already exist in symbol table
		if ( _symbolTable.hasName("dyld_stub_binding_helper") ) {
			SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("dyld_stub_binding_helper");
			_internal.classicBindingHelper = _internal.indirectBindingTable[slot];
		}
	}
	
	_internal.lazyBindingHelper = NULL;
	if ( _options.usingLazyDylibLinking() ) {
		// "dyld_lazy_dylib_stub_binding_helper" comes from lazydylib1.o file, so should already exist in symbol table
		if ( _symbolTable.hasName("dyld_lazy_dylib_stub_binding_helper") ) {
			SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("dyld_lazy_dylib_stub_binding_helper");
			_internal.lazyBindingHelper = _internal.indirectBindingTable[slot];
		}
		if ( _internal.lazyBindingHelper == NULL )
			throw "symbol dyld_lazy_dylib_stub_binding_helper not defined (usually in lazydylib1.o)";
	}
	
	_internal.compressedFastBinderProxy = NULL;
	if ( needsStubHelper && _options.makeCompressedDyldInfo() ) { 
		// "dyld_stub_binder" comes from libSystem.dylib so will need to manually resolve
		if ( !_symbolTable.hasName("dyld_stub_binder") ) {
			_inputFiles.searchLibraries("dyld_stub_binder", true, false, false, *this);
		}
		if ( _symbolTable.hasName("dyld_stub_binder") ) {
			SymbolTable::IndirectBindingSlot slot = _symbolTable.findSlotForName("dyld_stub_binder");
			_internal.compressedFastBinderProxy = _internal.indirectBindingTable[slot];
		}
		if ( _internal.compressedFastBinderProxy == NULL ) {
			if ( _options.undefinedTreatment() != Options::kUndefinedError ) {
				// make proxy
				_internal.compressedFastBinderProxy = new UndefinedProxyAtom("dyld_stub_binder");
				this->doAtom(*_internal.compressedFastBinderProxy);
			}
		}
	}
}


void Resolver::fillInInternalState()
{
	// store atoms into their final section
	for (std::vector<const ld::Atom*>::iterator it = _atoms.begin(); it != _atoms.end(); ++it) {
		_internal.addAtom(**it);
	}
	
	// <rdar://problem/7783918> make sure there is a __text section so that codesigning works
	if ( (_options.outputKind() == Options::kDynamicLibrary) || (_options.outputKind() == Options::kDynamicBundle) )
		_internal.getFinalSection(*new ld::Section("__TEXT", "__text", ld::Section::typeCode));
}

void Resolver::fillInEntryPoint()
{
	_internal.entryPoint = this->entryPoint(true);
}



void Resolver::removeCoalescedAwayAtoms()
{
	const bool log = false;
	if ( log ) {
		fprintf(stderr, "removeCoalescedAwayAtoms() starts with %lu atoms\n", _atoms.size());
	}
	_atoms.erase(std::remove_if(_atoms.begin(), _atoms.end(), AtomCoalescedAway()), _atoms.end());
	if ( log ) {
		fprintf(stderr, "removeCoalescedAwayAtoms() after removing coalesced atoms, %lu remain\n", _atoms.size());
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			fprintf(stderr, "  atom=%p %s\n", *it, (*it)->name());
		}
	}
}

void Resolver::linkTimeOptimize()
{
	// only do work here if some llvm obj files where loaded
	if ( ! _haveLLVMObjs )
		return;

	// run LLVM lto code-gen
	lto::OptimizeOptions optOpt;
	optOpt.outputFilePath				= _options.outputFilePath();
	optOpt.tmpObjectFilePath			= _options.tempLtoObjectPath();
	optOpt.preserveAllGlobals			= _options.allGlobalsAreDeadStripRoots() || _options.hasExportRestrictList();
	optOpt.verbose						= _options.verbose();
	optOpt.saveTemps					= _options.saveTempFiles();
	optOpt.pie							= _options.positionIndependentExecutable();
	optOpt.mainExecutable				= _options.linkingMainExecutable();;
	optOpt.staticExecutable 			= (_options.outputKind() == Options::kStaticExecutable);
	optOpt.relocatable					= (_options.outputKind() == Options::kObjectFile);
	optOpt.allowTextRelocs				= _options.allowTextRelocs();
	optOpt.linkerDeadStripping			= _options.deadCodeStrip();
	optOpt.needsUnwindInfoSection		= _options.needsUnwindInfoSection();
	optOpt.keepDwarfUnwind				= _options.keepDwarfUnwind();
	optOpt.arch							= _options.architecture();
	optOpt.mcpu							= _options.mcpuLTO();
	optOpt.llvmOptions					= &_options.llvmOptions();
	
	std::vector<const ld::Atom*>		newAtoms;
	std::vector<const char*>			additionalUndefines; 
	if ( ! lto::optimize(_atoms, _internal, optOpt, *this, newAtoms, additionalUndefines) )
		return; // if nothing done
		
	
	// add all newly created atoms to _atoms and update symbol table
	for(std::vector<const ld::Atom*>::iterator it = newAtoms.begin(); it != newAtoms.end(); ++it)
		this->doAtom(**it);
		
	// some atoms might have been optimized way (marked coalesced), remove them
	this->removeCoalescedAwayAtoms();

	// run through all atoms again and make sure newly codegened atoms have references bound
	for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) 
		this->convertReferencesToIndirect(**it);

	// adjust section of any new
	for (std::vector<const AliasAtom*>::const_iterator it=_aliasesFromCmdLine.begin(); it != _aliasesFromCmdLine.end(); ++it) {
		const AliasAtom* aliasAtom = *it;
		// update fields in AliasAtom to match newly constructed mach-o atom
		aliasAtom->setFinalAliasOf();
	}
	
	// resolve new undefines (e.g calls to _malloc and _memcpy that llvm compiler conjures up)
	for(std::vector<const char*>::iterator uit = additionalUndefines.begin(); uit != additionalUndefines.end(); ++uit) {
		const char *targetName = *uit;
		// these symbols may or may not already be in linker's symbol table
		if ( ! _symbolTable.hasName(targetName) ) {
			_inputFiles.searchLibraries(targetName, true, true, false, *this);
		}
	}

	// if -dead_strip on command line
	if ( _options.deadCodeStrip() ) {
		// clear liveness bit
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			(const_cast<ld::Atom*>(*it))->setLive((*it)->dontDeadStrip());
		}
		// and re-compute dead code
		this->deadStripOptimize(true);
	}
	
	// <rdar://problem/12386559> if -exported_symbols_list on command line, re-force scope
	if ( _options.hasExportMaskList() ) {
		for (std::vector<const ld::Atom*>::const_iterator it=_atoms.begin(); it != _atoms.end(); ++it) {
			const ld::Atom* atom = *it;
			if ( atom->scope() == ld::Atom::scopeGlobal ) {
				if ( !_options.shouldExport(atom->name()) ) {
					(const_cast<ld::Atom*>(atom))->setScope(ld::Atom::scopeLinkageUnit);
				}
			}
		}
	}
	
	if ( _options.outputKind() == Options::kObjectFile ) {
		// if -r mode, add proxies for new undefines (e.g. ___stack_chk_fail)
		this->resolveUndefines();
	}
	else {
		// last chance to check for undefines
		this->checkUndefines(true);

		// check new code does not override some dylib
		this->checkDylibSymbolCollisions();
	}
}


void Resolver::tweakWeakness()
{
	// <rdar://problem/7977374> Add command line options to control symbol weak-def bit on exported symbols			
	if ( _options.hasWeakBitTweaks() ) {
		for (std::vector<ld::Internal::FinalSection*>::iterator sit = _internal.sections.begin(); sit != _internal.sections.end(); ++sit) {
			ld::Internal::FinalSection* sect = *sit;
			for (std::vector<const ld::Atom*>::iterator ait = sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* atom = *ait;
				if ( atom->definition() != ld::Atom::definitionRegular ) 
					continue;
				const char* name = atom->name();
				if ( atom->scope() == ld::Atom::scopeGlobal ) {
					if ( atom->combine() == ld::Atom::combineNever ) {
						if ( _options.forceWeak(name) )
							(const_cast<ld::Atom*>(atom))->setCombine(ld::Atom::combineByName);
					}
					else if ( atom->combine() == ld::Atom::combineByName ) {
						if ( _options.forceNotWeak(name) )
							(const_cast<ld::Atom*>(atom))->setCombine(ld::Atom::combineNever);
					}
				}
				else {
					if ( _options.forceWeakNonWildCard(name) )
						warning("cannot force to be weak, non-external symbol %s", name);
					else if ( _options.forceNotWeakNonWildcard(name) )
						warning("cannot force to be not-weak, non-external symbol %s", name);
				}
			}
		}
	}
}


void Resolver::resolve()
{
	this->initializeState();
	this->buildAtomList();
	this->addInitialUndefines();
	this->fillInHelpersInInternalState();
	this->resolveUndefines();
	this->deadStripOptimize();
	this->checkUndefines();
	this->checkDylibSymbolCollisions();
	this->removeCoalescedAwayAtoms();
	this->fillInEntryPoint();
	this->linkTimeOptimize();
	this->fillInInternalState();
	this->tweakWeakness();
    _symbolTable.checkDuplicateSymbols();
}



} // namespace tool 
} // namespace ld 



