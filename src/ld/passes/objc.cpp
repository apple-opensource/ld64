/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2010 Apple Inc. All rights reserved.
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
#include <dlfcn.h>
#include <mach/machine.h>

#include <vector>
#include <map>
#include <set>

#include "Architectures.hpp"
#include "MachOFileAbstraction.hpp"

#include "ld.hpp"
#include "objc.h"

namespace ld {
namespace passes {
namespace objc {



struct objc_image_info  {
	uint32_t	version;	// initially 0
	uint32_t	flags;
};

#define OBJC_IMAGE_IS_REPLACEMENT		(1<<0)
#define OBJC_IMAGE_SUPPORTS_GC			(1<<1)
#define OBJC_IMAGE_REQUIRES_GC			(1<<2)
#define OBJC_IMAGE_OPTIMIZED_BY_DYLD	(1<<3)
#define OBJC_IMAGE_SUPPORTS_COMPACTION	(1<<4)



//
// This class is the 8 byte section containing ObjC flags
//
template <typename A>
class ObjCImageInfoAtom : public ld::Atom {
public:
											ObjCImageInfoAtom(ld::File::ObjcConstraint objcConstraint, 
															bool compaction, bool objcReplacementClasses, bool abi2);

	virtual const ld::File*					file() const					{ return NULL; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return "objc image info"; }
	virtual uint64_t						size() const					{ return sizeof(objc_image_info); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		memcpy(buffer, &_content, sizeof(objc_image_info));
	}

private:	
	objc_image_info							_content;

	static ld::Section						_s_sectionABI1;
	static ld::Section						_s_sectionABI2;
};

template <typename A> ld::Section ObjCImageInfoAtom<A>::_s_sectionABI1("__OBJC", "__image_info", ld::Section::typeUnclassified);
template <typename A> ld::Section ObjCImageInfoAtom<A>::_s_sectionABI2("__DATA", "__objc_imageinfo", ld::Section::typeUnclassified);


template <typename A>
ObjCImageInfoAtom<A>::ObjCImageInfoAtom(ld::File::ObjcConstraint objcConstraint, bool compaction, 
										bool objcReplacementClasses, bool abi2)
	: ld::Atom(abi2 ? _s_sectionABI2 : _s_sectionABI1, ld::Atom::definitionRegular, ld::Atom::combineNever,
							ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
							symbolTableNotIn, false, false, false, ld::Atom::Alignment(2))
{  
	
	uint32_t value = 0;
	if ( objcReplacementClasses ) 
		value = OBJC_IMAGE_IS_REPLACEMENT;
	switch ( objcConstraint ) {
		case ld::File::objcConstraintNone:
		case ld::File::objcConstraintRetainRelease:
			if ( compaction ) 
				warning("ignoring -objc_gc_compaction because code not compiled for ObjC garbage collection");
			break;
		case ld::File::objcConstraintRetainReleaseOrGC:
			value |= OBJC_IMAGE_SUPPORTS_GC;
		if ( compaction ) 
			value |= OBJC_IMAGE_SUPPORTS_COMPACTION;
			break;
		case ld::File::objcConstraintGC:
			value |= OBJC_IMAGE_SUPPORTS_GC | OBJC_IMAGE_REQUIRES_GC;
			if ( compaction ) 
				value |= OBJC_IMAGE_SUPPORTS_COMPACTION;
			break;
	}

	_content.version = 0;
	A::P::E::set32(_content.flags, value);
}



//
// This class is for a new Atom which is an ObjC method list created by merging method lists from categories
//
template <typename A>
class MethodListAtom : public ld::Atom {
public:
											MethodListAtom(ld::Internal& state, const ld::Atom* baseMethodList, bool meta, 
															const std::vector<const ld::Atom*>* categories, 
															std::set<const ld::Atom*>& deadAtoms);

	virtual const ld::File*					file() const					{ return _file; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return "objc merged method list"; }
	virtual uint64_t						size() const					{ return _methodCount*3*sizeof(pint_t) + 8; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		bzero(buffer, size());
		A::P::E::set32(*((uint32_t*)(&buffer[0])), 24);
		A::P::E::set32(*((uint32_t*)(&buffer[4])), _methodCount);
	}
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

private:	
	typedef typename A::P::uint_t			pint_t;

	const ld::File*							_file;
	unsigned int							_methodCount;
	std::vector<ld::Fixup>					_fixups;
	
	static ld::Section						_s_section;
};

template <typename A> 
ld::Section MethodListAtom<A>::_s_section("__DATA", "__objc_const", ld::Section::typeUnclassified);


//
// This class is for a new Atom which is an ObjC protocol list created by merging protocol lists from categories
//
template <typename A>
class ProtocolListAtom : public ld::Atom {
public:
											ProtocolListAtom(ld::Internal& state, const ld::Atom* baseProtocolList, 
															const std::vector<const ld::Atom*>* categories, 
															std::set<const ld::Atom*>& deadAtoms);

	virtual const ld::File*					file() const					{ return _file; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return "objc merged protocol list"; }
	virtual uint64_t						size() const					{ return (_protocolCount+1)*sizeof(pint_t); }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		bzero(buffer, size());
		A::P::setP(*((pint_t*)(buffer)), _protocolCount);
	}
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

private:	
	typedef typename A::P::uint_t			pint_t;

	const ld::File*							_file;
	unsigned int							_protocolCount;
	std::vector<ld::Fixup>					_fixups;
	
	static ld::Section						_s_section;
};

template <typename A> 
ld::Section ProtocolListAtom<A>::_s_section("__DATA", "__objc_const", ld::Section::typeUnclassified);



//
// This class is for a new Atom which is an ObjC property list created by merging property lists from categories
//
template <typename A>
class PropertyListAtom : public ld::Atom {
public:
											PropertyListAtom(ld::Internal& state, const ld::Atom* baseProtocolList, 
															const std::vector<const ld::Atom*>* categories, 
															std::set<const ld::Atom*>& deadAtoms);

	virtual const ld::File*					file() const					{ return _file; }
	virtual bool							translationUnitSource(const char** dir, const char**) const 
																			{ return false; }
	virtual const char*						name() const					{ return "objc merged property list"; }
	virtual uint64_t						size() const					{ return _propertyCount*2*sizeof(pint_t) + 8; }
	virtual uint64_t						objectAddress() const			{ return 0; }
	virtual void							setScope(Scope)					{ }
	virtual void							copyRawContent(uint8_t buffer[]) const {
		bzero(buffer, size());
		A::P::E::set32(((uint32_t*)(buffer))[0], 2*sizeof(pint_t)); // sizeof(objc_property)
		A::P::E::set32(((uint32_t*)(buffer))[1], _propertyCount);
	}
	virtual ld::Fixup::iterator				fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator				fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

private:	
	typedef typename A::P::uint_t			pint_t;

	const ld::File*							_file;
	unsigned int							_propertyCount;
	std::vector<ld::Fixup>					_fixups;
	
	static ld::Section						_s_section;
};

template <typename A> 
ld::Section PropertyListAtom<A>::_s_section("__DATA", "__objc_const", ld::Section::typeUnclassified);





//
// This class is used to create an Atom that replaces an atom from a .o file that holds a class_ro_t.
// It is needed because there is no way to add Fixups to an existing atom.
//
template <typename A>
class ClassROOverlayAtom : public ld::Atom {
public:
											ClassROOverlayAtom(const ld::Atom* classROAtom);

	// overrides of ld::Atom
	virtual const ld::File*				file() const		{ return _atom->file(); }
	virtual bool						translationUnitSource(const char** dir, const char** nm) const
															{ return _atom->translationUnitSource(dir, nm); }
	virtual const char*					name() const		{ return _atom->name(); }
	virtual uint64_t					size() const		{ return _atom->size(); }
	virtual uint64_t					objectAddress() const { return _atom->objectAddress(); }
	virtual void						copyRawContent(uint8_t buffer[]) const 
															{ _atom->copyRawContent(buffer); }
	virtual const uint8_t*				rawContentPointer() const 
															{ return _atom->rawContentPointer(); }
	virtual unsigned long				contentHash(const class ld::IndirectBindingTable& ibt) const 
															{ return _atom->contentHash(ibt); }
	virtual bool						canCoalesceWith(const ld::Atom& rhs, const class ld::IndirectBindingTable& ibt) const 
															{ return _atom->canCoalesceWith(rhs,ibt); }

	virtual ld::Fixup::iterator			fixupsBegin() const	{ return (ld::Fixup*)&_fixups[0]; }
	virtual ld::Fixup::iterator			fixupsEnd()	const	{ return (ld::Fixup*)&_fixups[_fixups.size()]; }

	void								addProtocolListFixup();
	void								addPropertyListFixup();
	void								addMethodListFixup();

private:	
	typedef typename A::P::uint_t			pint_t;

	const ld::Atom*							_atom;
	std::vector<ld::Fixup>					_fixups;
};

template <typename A>
ClassROOverlayAtom<A>::ClassROOverlayAtom(const ld::Atom* classROAtom)
	: ld::Atom(classROAtom->section(), ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
			classROAtom->symbolTableInclusion(), false, false, false, classROAtom->alignment()),
	_atom(classROAtom)
{
	// ensure all attributes are same as original
	this->setAttributesFromAtom(*classROAtom);

	// copy fixups from orginal atom
	for (ld::Fixup::iterator fit=classROAtom->fixupsBegin(); fit != classROAtom->fixupsEnd(); ++fit) {
		ld::Fixup fixup = *fit;
		_fixups.push_back(fixup);
	}
}


//
// Base class for reading and updating existing ObjC atoms from .o files
//
template <typename A>
class ObjCData {
public:
	static const ld::Atom*	getPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, unsigned int offset, bool* hasAddend=NULL);
	static void				setPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, 
												unsigned int offset, const ld::Atom* newAtom);
	typedef typename A::P::uint_t			pint_t;
};

template <typename A>
const ld::Atom* ObjCData<A>::getPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, unsigned int offset, bool* hasAddend)
{
	const ld::Atom* target = NULL;
	if ( hasAddend != NULL )
		*hasAddend = false;
	for (ld::Fixup::iterator fit=contentAtom->fixupsBegin(); fit != contentAtom->fixupsEnd(); ++fit) {
		if ( fit->offsetInAtom == offset ) {
			switch ( fit->binding ) {
				case ld::Fixup::bindingsIndirectlyBound:
					target = state.indirectBindingTable[fit->u.bindingIndex];
					break;
				case ld::Fixup::bindingDirectlyBound:
					target =  fit->u.target;
					break;
				case ld::Fixup::bindingNone:
					if ( fit->kind == ld::Fixup::kindAddAddend ) {
						if ( hasAddend != NULL )
							*hasAddend = true;
					}
					break;
                 default:
                    break;   
			}
		}
	}
	return target;
}

template <typename A>
void ObjCData<A>::setPointerInContent(ld::Internal& state, const ld::Atom* contentAtom, 
														unsigned int offset, const ld::Atom* newAtom)
{
	for (ld::Fixup::iterator fit=contentAtom->fixupsBegin(); fit != contentAtom->fixupsEnd(); ++fit) {
		if ( fit->offsetInAtom == offset ) {
			switch ( fit->binding ) {
				case ld::Fixup::bindingsIndirectlyBound:
					state.indirectBindingTable[fit->u.bindingIndex] = newAtom;
					return;
				case ld::Fixup::bindingDirectlyBound:
					fit->u.target = newAtom;
					return;
                default:
                     break;    
			}
		}
	}
	assert(0 && "could not update method list");
}



//
// Helper class for reading and updating existing ObjC category atoms from .o files
//
template <typename A>
class Category : public ObjCData<A> {
public:
	static const ld::Atom*	getClass(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getInstanceMethods(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getClassMethods(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getProtocols(ld::Internal& state, const ld::Atom* contentAtom);
	static const ld::Atom*	getProperties(ld::Internal& state, const ld::Atom* contentAtom);
	static uint32_t         size() { return 6*sizeof(pint_t); }
private:	
	typedef typename A::P::uint_t			pint_t;
};


template <typename A>
const ld::Atom*	Category<A>::getClass(ld::Internal& state, const ld::Atom* contentAtom)
{
	return ObjCData<A>::getPointerInContent(state, contentAtom, sizeof(pint_t)); // category_t.cls
}

template <typename A>
const ld::Atom*	Category<A>::getInstanceMethods(ld::Internal& state, const ld::Atom* contentAtom)
{
	return ObjCData<A>::getPointerInContent(state, contentAtom, 2*sizeof(pint_t)); // category_t.instanceMethods
}

template <typename A>
const ld::Atom*	Category<A>::getClassMethods(ld::Internal& state, const ld::Atom* contentAtom)
{
	return ObjCData<A>::getPointerInContent(state, contentAtom, 3*sizeof(pint_t)); // category_t.classMethods
}

template <typename A>
const ld::Atom*	Category<A>::getProtocols(ld::Internal& state, const ld::Atom* contentAtom)
{
	return ObjCData<A>::getPointerInContent(state, contentAtom, 4*sizeof(pint_t)); // category_t.protocols
}

template <typename A>
const ld::Atom*	Category<A>::getProperties(ld::Internal& state, const ld::Atom* contentAtom)
{
	return ObjCData<A>::getPointerInContent(state, contentAtom, 5*sizeof(pint_t)); // category_t.instanceProperties
}


template <typename A>
class MethodList : public ObjCData<A> {
public:
	static uint32_t	count(ld::Internal& state, const ld::Atom* methodListAtom) {
		const uint32_t* methodListData = (uint32_t*)(methodListAtom->rawContentPointer());
		return A::P::E::get32(methodListData[1]); // method_list_t.count
	}
};

template <typename A>
class ProtocolList : public ObjCData<A> {
public:
	static uint32_t	count(ld::Internal& state, const ld::Atom* protocolListAtom)  {
		pint_t* protocolListData = (pint_t*)(protocolListAtom->rawContentPointer());
		return A::P::getP(*protocolListData); // protocol_list_t.count
	}
private:
	typedef typename A::P::uint_t	pint_t;
};

template <typename A>
class PropertyList : public ObjCData<A> {
public:
	static uint32_t	count(ld::Internal& state, const ld::Atom* protocolListAtom)  {
		uint32_t* protocolListData = (uint32_t*)(protocolListAtom->rawContentPointer());
		return A::P::E::get32(protocolListData[1]); // property_list_t.count
	}
private:
	typedef typename A::P::uint_t	pint_t;
};



//
// Helper class for reading and updating existing ObjC class atoms from .o files
//
template <typename A>
class Class : public ObjCData<A> {
public:
	static const ld::Atom*	getInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	getClassMethodList(ld::Internal& state, const ld::Atom* classAtom);
	static const ld::Atom*	setInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* methodListAtom, std::set<const ld::Atom*>& deadAtoms);
	static const ld::Atom*	setInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms);
	static const ld::Atom*	setInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* propertyListAtom, std::set<const ld::Atom*>& deadAtoms);
	static const ld::Atom*  setClassMethodList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* methodListAtom, std::set<const ld::Atom*>& deadAtoms);
	static const ld::Atom*	setClassProtocolList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms);
	static uint32_t         size() { return 5*sizeof(pint_t); }
	static unsigned int		class_ro_header_size();
private:
	typedef typename A::P::uint_t			pint_t;
	static const ld::Atom*	getROData(ld::Internal& state, const ld::Atom* classAtom);
};

template <> unsigned int Class<x86_64>::class_ro_header_size() { return 16; }
template <> unsigned int Class<arm>::class_ro_header_size() { return 12;}
template <> unsigned int Class<x86>::class_ro_header_size() { return 12; }


template <typename A>
const ld::Atom*	Class<A>::getROData(ld::Internal& state, const ld::Atom* classAtom)
{
	return ObjCData<A>::getPointerInContent(state, classAtom, 4*sizeof(pint_t)); // class_t.data

}

template <typename A>
const ld::Atom*	Class<A>::getInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom)
{
	const ld::Atom* classROAtom = getROData(state, classAtom); // class_t.data
	assert(classROAtom != NULL);
	return ObjCData<A>::getPointerInContent(state, classROAtom, class_ro_header_size() + 2*sizeof(pint_t)); // class_ro_t.baseMethods
}

template <typename A>
const ld::Atom*	Class<A>::getInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom)
{
	const ld::Atom* classROAtom = getROData(state, classAtom); // class_t.data
	assert(classROAtom != NULL);
	return ObjCData<A>::getPointerInContent(state, classROAtom, class_ro_header_size() + 3*sizeof(pint_t)); // class_ro_t.baseProtocols
}

template <typename A>
const ld::Atom*	Class<A>::getInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom)
{
	const ld::Atom* classROAtom = getROData(state, classAtom); // class_t.data
	assert(classROAtom != NULL);
	return ObjCData<A>::getPointerInContent(state, classROAtom, class_ro_header_size() + 6*sizeof(pint_t)); // class_ro_t.baseProperties
}

template <typename A>
const ld::Atom*	Class<A>::getClassMethodList(ld::Internal& state, const ld::Atom* classAtom)
{
	const ld::Atom* metaClassAtom = ObjCData<A>::getPointerInContent(state, classAtom, 0); // class_t.isa
	assert(metaClassAtom != NULL);
	return Class<A>::getInstanceMethodList(state, metaClassAtom);
}

template <typename A>
const ld::Atom* Class<A>::setInstanceMethodList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* methodListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	const ld::Atom* classROAtom = getROData(state, classAtom); // class_t.data
	assert(classROAtom != NULL);
	// if the base class does not already have a method list, we need to create an overlay
	if ( getInstanceMethodList(state, classAtom) == NULL ) {
		ClassROOverlayAtom<A>* overlay = new ClassROOverlayAtom<A>(classROAtom);
		//fprintf(stderr, "replace class RO atom %p with %p for method list in class atom %s\n", classROAtom, overlay, classAtom->name());
		overlay->addMethodListFixup();
		ObjCData<A>::setPointerInContent(state, classAtom, 4*sizeof(pint_t), overlay); // class_t.data
		deadAtoms.insert(classROAtom);
		ObjCData<A>::setPointerInContent(state, overlay, class_ro_header_size() + 2*sizeof(pint_t), methodListAtom); // class_ro_t.baseMethods
		return overlay;
	}
	ObjCData<A>::setPointerInContent(state, classROAtom, class_ro_header_size() + 2*sizeof(pint_t), methodListAtom); // class_ro_t.baseMethods
	return NULL; // means classRO atom was not replaced
}

template <typename A>
const ld::Atom* Class<A>::setInstanceProtocolList(ld::Internal& state, const ld::Atom* classAtom, 
									const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	const ld::Atom* classROAtom = getROData(state, classAtom); // class_t.data
	assert(classROAtom != NULL);
	// if the base class does not already have a protocol list, we need to create an overlay
	if ( getInstanceProtocolList(state, classAtom) == NULL ) {
		ClassROOverlayAtom<A>* overlay = new ClassROOverlayAtom<A>(classROAtom);
		//fprintf(stderr, "replace class RO atom %p with %p for protocol list in class atom %s\n", classROAtom, overlay, classAtom->name());
		overlay->addProtocolListFixup();
		ObjCData<A>::setPointerInContent(state, classAtom, 4*sizeof(pint_t), overlay); // class_t.data
		deadAtoms.insert(classROAtom);
		ObjCData<A>::setPointerInContent(state, overlay, class_ro_header_size() + 3*sizeof(pint_t), protocolListAtom); // class_ro_t.baseProtocols
		return overlay;
	}
	//fprintf(stderr, "set class RO atom %p protocol list in class atom %s\n", classROAtom, classAtom->name());
	ObjCData<A>::setPointerInContent(state, classROAtom, class_ro_header_size() + 3*sizeof(pint_t), protocolListAtom); // class_ro_t.baseProtocols
	return NULL;  // means classRO atom was not replaced
}

template <typename A>
const ld::Atom* Class<A>::setClassProtocolList(ld::Internal& state, const ld::Atom* classAtom, 
									const ld::Atom* protocolListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	// meta class also points to same protocol list as class
	const ld::Atom* metaClassAtom = ObjCData<A>::getPointerInContent(state, classAtom, 0); // class_t.isa
	//fprintf(stderr, "setClassProtocolList(), classAtom=%p %s, metaClass=%p %s\n", classAtom, classAtom->name(), metaClassAtom, metaClassAtom->name());
	assert(metaClassAtom != NULL);
	return setInstanceProtocolList(state, metaClassAtom, protocolListAtom, deadAtoms);
}



template <typename A>
const ld::Atom*  Class<A>::setInstancePropertyList(ld::Internal& state, const ld::Atom* classAtom, 
												const ld::Atom* propertyListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	const ld::Atom* classROAtom = getROData(state, classAtom); // class_t.data
	assert(classROAtom != NULL);
	// if the base class does not already have a property list, we need to create an overlay
	if ( getInstancePropertyList(state, classAtom) == NULL ) {
		ClassROOverlayAtom<A>* overlay = new ClassROOverlayAtom<A>(classROAtom);
		//fprintf(stderr, "replace class RO atom %p with %p for property list in class atom %s\n", classROAtom, overlay, classAtom->name());
		overlay->addPropertyListFixup();
		ObjCData<A>::setPointerInContent(state, classAtom, 4*sizeof(pint_t), overlay); // class_t.data
		deadAtoms.insert(classROAtom);
		ObjCData<A>::setPointerInContent(state, overlay, class_ro_header_size() + 6*sizeof(pint_t), propertyListAtom); // class_ro_t.baseProperties
		return overlay;
	}
	ObjCData<A>::setPointerInContent(state, classROAtom, class_ro_header_size() + 6*sizeof(pint_t), propertyListAtom); // class_ro_t.baseProperties
	return NULL;  // means classRO atom was not replaced
}

template <typename A>
const ld::Atom* Class<A>::setClassMethodList(ld::Internal& state, const ld::Atom* classAtom, 
											const ld::Atom* methodListAtom, std::set<const ld::Atom*>& deadAtoms)
{
	// class methods is just instance methods of metaClass
	const ld::Atom* metaClassAtom = ObjCData<A>::getPointerInContent(state, classAtom, 0); // class_t.isa
	assert(metaClassAtom != NULL);
	return setInstanceMethodList(state, metaClassAtom, methodListAtom, deadAtoms);
}



template <>
void ClassROOverlayAtom<x86_64>::addMethodListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<x86_64>::class_ro_header_size() + 2*8; // class_ro_t.baseMethods
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian64, targetAtom));
}

template <>
void ClassROOverlayAtom<arm>::addMethodListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<arm>::class_ro_header_size() + 2*4; // class_ro_t.baseMethods
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian32, targetAtom));
}

template <>
void ClassROOverlayAtom<x86>::addMethodListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<x86>::class_ro_header_size() + 2*4; // class_ro_t.baseMethods
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian32, targetAtom));
}



template <>
void ClassROOverlayAtom<x86_64>::addProtocolListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<x86_64>::class_ro_header_size() + 3*8; // class_ro_t.baseProtocols
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian64, targetAtom));
}

template <>
void ClassROOverlayAtom<arm>::addProtocolListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<arm>::class_ro_header_size() + 3*4; // class_ro_t.baseProtocols
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian32, targetAtom));
}

template <>
void ClassROOverlayAtom<x86>::addProtocolListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<x86>::class_ro_header_size() + 3*4; // class_ro_t.baseProtocols
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian32, targetAtom));
}


template <>
void ClassROOverlayAtom<x86_64>::addPropertyListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<x86_64>::class_ro_header_size() + 6*8; // class_ro_t.baseProperties
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian64, targetAtom));
}

template <>
void ClassROOverlayAtom<arm>::addPropertyListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<arm>::class_ro_header_size() + 6*4; // class_ro_t.baseProperties
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian32, targetAtom));
}

template <>
void ClassROOverlayAtom<x86>::addPropertyListFixup()
{
	const ld::Atom* targetAtom = this; // temporary
	uint32_t offset = Class<x86>::class_ro_header_size() + 6*4; // class_ro_t.baseProperties
	_fixups.push_back(ld::Fixup(offset, ld::Fixup::k1of1, ld::Fixup::kindStoreTargetAddressLittleEndian32, targetAtom));
}




//
// Encapsulates merging of ObjC categories
//
template <typename A>
class OptimizeCategories {
public:
	static void				doit(const Options& opts, ld::Internal& state);
	static bool				hasInstanceMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasClassMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasProtocols(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	static bool				hasProperties(ld::Internal& state, const std::vector<const ld::Atom*>* categories);
	
	
	static unsigned int		class_ro_baseMethods_offset();
private:
	typedef typename A::P::uint_t			pint_t;

};


template <typename A>
bool OptimizeCategories<A>::hasInstanceMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* methodList = Category<A>::getInstanceMethods(state, categoryAtom);
		if ( methodList != NULL ) {
			if ( MethodList<A>::count(state, methodList) > 0 )
				return true;
		}
	}
	return false;
}


template <typename A>
bool OptimizeCategories<A>::hasClassMethods(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* methodList = Category<A>::getClassMethods(state, categoryAtom);
		if ( methodList != NULL ) {
			if ( MethodList<A>::count(state, methodList) > 0 )
				return true;
		}
	}
	return false;
}

template <typename A>
bool OptimizeCategories<A>::hasProtocols(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* protocolListAtom = Category<A>::getProtocols(state, categoryAtom);
		if ( protocolListAtom != NULL ) {
			if ( ProtocolList<A>::count(state, protocolListAtom) > 0 ) {	
				return true;
			}
		}
	}
	return false;
}


template <typename A>
bool OptimizeCategories<A>::hasProperties(ld::Internal& state, const std::vector<const ld::Atom*>* categories)
{
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryAtom = *it;
		const ld::Atom* propertyListAtom = Category<A>::getProperties(state, categoryAtom);
		if ( propertyListAtom != NULL ) {
			if ( PropertyList<A>::count(state, propertyListAtom) > 0 )
				return true;
		}
	}
	return false;
}



//
// Helper for std::remove_if
//
class OptimizedAway {
public:
	OptimizedAway(const std::set<const ld::Atom*>& oa) : _dead(oa) {}
	bool operator()(const ld::Atom* atom) const {
		return ( _dead.count(atom) != 0 );
	}
private:
	const std::set<const ld::Atom*>& _dead;
};

template <typename A>
void OptimizeCategories<A>::doit(const Options& opts, ld::Internal& state)
{
	// first find all categories referenced by __objc_nlcatlist section
	std::set<const ld::Atom*> nlcatListAtoms;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( (strcmp(sect->sectionName(), "__objc_nlcatlist") == 0) && (strcmp(sect->segmentName(), "__DATA") == 0) ) {
			for (std::vector<const ld::Atom*>::iterator ait=sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* categoryListElementAtom = *ait;
				for (unsigned int offset=0; offset < categoryListElementAtom->size(); offset += sizeof(pint_t)) {
					const ld::Atom* categoryAtom = ObjCData<A>::getPointerInContent(state, categoryListElementAtom, offset);
					//fprintf(stderr, "offset=%d, cat=%p %s\n", offset, categoryAtom, categoryAtom->name());
					assert(categoryAtom != NULL);
					nlcatListAtoms.insert(categoryAtom);
				}
			}
		}
	}
	
	// build map of all classes in this image that have categories on them
	typedef std::map<const ld::Atom*, std::vector<const ld::Atom*>*> CatMap;
	CatMap classToCategories;
	std::set<const ld::Atom*> deadAtoms;
	ld::Internal::FinalSection* methodListSection = NULL;
	for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
		ld::Internal::FinalSection* sect = *sit;
		if ( sect->type() == ld::Section::typeObjC2CategoryList ) {
			for (std::vector<const ld::Atom*>::iterator ait=sect->atoms.begin(); ait != sect->atoms.end(); ++ait) {
				const ld::Atom* categoryListElementAtom = *ait;
				bool hasAddend;
				const ld::Atom* categoryAtom = ObjCData<A>::getPointerInContent(state, categoryListElementAtom, 0, &hasAddend);
				if ( hasAddend || (categoryAtom->symbolTableInclusion() ==  ld::Atom::symbolTableNotIn)) {
					//<rdar://problem/8309530> gcc-4.0 uses 'L' labels on categories which disables this optimization
					//warning("__objc_catlist element does not point to start of category");
					continue;
				}
				assert(categoryAtom != NULL);
				assert(categoryAtom->size() == Category<A>::size());
				// ignore categories also in __objc_nlcatlist
				if ( nlcatListAtoms.count(categoryAtom) != 0 )
					continue;
				const ld::Atom* categoryOnClassAtom = Category<A>::getClass(state, categoryAtom); 
				assert(categoryOnClassAtom != NULL);
				if ( categoryOnClassAtom->definition() != ld::Atom::definitionProxy ) {
					// only look at classes defined in this image
					CatMap::iterator pos = classToCategories.find(categoryOnClassAtom);
					if ( pos == classToCategories.end() ) {
						classToCategories[categoryOnClassAtom] = new std::vector<const ld::Atom*>();
					}
					classToCategories[categoryOnClassAtom]->push_back(categoryAtom);
					// mark category atom and catlist atom as dead
					deadAtoms.insert(categoryAtom);
					deadAtoms.insert(categoryListElementAtom);
				}
			}
		}
		// record method list section
		if ( (strcmp(sect->sectionName(), "__objc_const") == 0) && (strcmp(sect->segmentName(), "__DATA") == 0) )
			methodListSection = sect;
	}

	// if found some categories
	if ( classToCategories.size() != 0 ) {
		assert(methodListSection != NULL);
		// alter each class definition to have new method list which includes all category methods
		for (CatMap::iterator it=classToCategories.begin(); it != classToCategories.end(); ++it) {
			const ld::Atom* classAtom = it->first;
			const std::vector<const ld::Atom*>* categories = it->second;
			assert(categories->size() != 0);
			// if any category adds instance methods, generate new merged method list, and replace
			if ( OptimizeCategories<A>::hasInstanceMethods(state, categories) ) { 
				const ld::Atom* baseInstanceMethodListAtom = Class<A>::getInstanceMethodList(state, classAtom); 
				const ld::Atom* newInstanceMethodListAtom = new MethodListAtom<A>(state, baseInstanceMethodListAtom, false, categories, deadAtoms);
				const ld::Atom* newClassRO = Class<A>::setInstanceMethodList(state, classAtom, newInstanceMethodListAtom, deadAtoms);
				// add new method list to final sections
				methodListSection->atoms.push_back(newInstanceMethodListAtom);
				if ( newClassRO != NULL ) {
					assert(strcmp(newClassRO->section().sectionName(), "__objc_const") == 0);
					methodListSection->atoms.push_back(newClassRO);
				}
			}
			// if any category adds class methods, generate new merged method list, and replace
			if ( OptimizeCategories<A>::hasClassMethods(state, categories) ) { 
				const ld::Atom* baseClassMethodListAtom = Class<A>::getClassMethodList(state, classAtom); 
				const ld::Atom* newClassMethodListAtom = new MethodListAtom<A>(state, baseClassMethodListAtom, true, categories, deadAtoms);
				const ld::Atom* newClassRO = Class<A>::setClassMethodList(state, classAtom, newClassMethodListAtom, deadAtoms);
				// add new method list to final sections
				methodListSection->atoms.push_back(newClassMethodListAtom);
				if ( newClassRO != NULL ) {
					assert(strcmp(newClassRO->section().sectionName(), "__objc_const") == 0);
					methodListSection->atoms.push_back(newClassRO);
				}
			}
			// if any category adds protocols, generate new merged protocol list, and replace
			if ( OptimizeCategories<A>::hasProtocols(state, categories) ) { 
				const ld::Atom* baseProtocolListAtom = Class<A>::getInstanceProtocolList(state, classAtom); 
				const ld::Atom* newProtocolListAtom = new ProtocolListAtom<A>(state, baseProtocolListAtom, categories, deadAtoms);
				const ld::Atom* newClassRO = Class<A>::setInstanceProtocolList(state, classAtom, newProtocolListAtom, deadAtoms);
				const ld::Atom* newMetaClassRO = Class<A>::setClassProtocolList(state, classAtom, newProtocolListAtom, deadAtoms);
				// add new protocol list to final sections
				methodListSection->atoms.push_back(newProtocolListAtom);
				if ( newClassRO != NULL ) {
					assert(strcmp(newClassRO->section().sectionName(), "__objc_const") == 0);
					methodListSection->atoms.push_back(newClassRO);
				}
				if ( newMetaClassRO != NULL ) {
					assert(strcmp(newMetaClassRO->section().sectionName(), "__objc_const") == 0);
					methodListSection->atoms.push_back(newMetaClassRO);
				}
			}
			// if any category adds properties, generate new merged property list, and replace
			if ( OptimizeCategories<A>::hasProperties(state, categories) ) { 
				const ld::Atom* basePropertyListAtom = Class<A>::getInstancePropertyList(state, classAtom); 
				const ld::Atom* newPropertyListAtom = new PropertyListAtom<A>(state, basePropertyListAtom, categories, deadAtoms);
				const ld::Atom* newClassRO = Class<A>::setInstancePropertyList(state, classAtom, newPropertyListAtom, deadAtoms);
				// add new property list to final sections
				methodListSection->atoms.push_back(newPropertyListAtom);
				if ( newClassRO != NULL ) {
					assert(strcmp(newClassRO->section().sectionName(), "__objc_const") == 0);
					methodListSection->atoms.push_back(newClassRO);
				}
			}
		 
		}

		// remove dead atoms
		for (std::vector<ld::Internal::FinalSection*>::iterator sit=state.sections.begin(); sit != state.sections.end(); ++sit) {
			ld::Internal::FinalSection* sect = *sit;
			sect->atoms.erase(std::remove_if(sect->atoms.begin(), sect->atoms.end(), OptimizedAway(deadAtoms)), sect->atoms.end());
		}
	}
}


template <typename A> 
MethodListAtom<A>::MethodListAtom(ld::Internal& state, const ld::Atom* baseMethodList, bool meta, 
									const std::vector<const ld::Atom*>* categories, std::set<const ld::Atom*>& deadAtoms)
  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
			symbolTableNotIn, false, false, false, ld::Atom::Alignment(3)), _file(NULL), _methodCount(0) 
{
	unsigned int fixupCount = 0;
	// if base class has method list, then associate new method list with file defining class
	if ( baseMethodList != NULL ) {
		_file = baseMethodList->file();
		// calculate total size of merge method lists
		_methodCount = MethodList<A>::count(state, baseMethodList);
		deadAtoms.insert(baseMethodList);
		fixupCount = baseMethodList->fixupsEnd() - baseMethodList->fixupsBegin();
	}
	for (std::vector<const ld::Atom*>::const_iterator ait=categories->begin(); ait != categories->end(); ++ait) {
		const ld::Atom* categoryMethodListAtom;
		if ( meta )
			categoryMethodListAtom = Category<A>::getClassMethods(state, *ait);
		else
			categoryMethodListAtom = Category<A>::getInstanceMethods(state, *ait);
		if ( categoryMethodListAtom != NULL ) {
			_methodCount += MethodList<A>::count(state, categoryMethodListAtom);
			fixupCount += (categoryMethodListAtom->fixupsEnd() - categoryMethodListAtom->fixupsBegin());
			deadAtoms.insert(categoryMethodListAtom);
			// if base class did not have method list, associate new method list with file the defined category
			if ( _file == NULL )
				_file = categoryMethodListAtom->file();
		}
	}
	//if ( baseMethodList != NULL )
	//	fprintf(stderr, "total merged method count=%u for baseMethodList=%s\n", _methodCount, baseMethodList->name());
	//else
	//	fprintf(stderr, "total merged method count=%u\n", _methodCount);
	//fprintf(stderr, "total merged fixup count=%u\n", fixupCount);
	
	// copy fixups and adjust offsets (in reverse order to simulator objc runtime)
	_fixups.reserve(fixupCount);
	uint32_t slide = 0;
	for (std::vector<const ld::Atom*>::const_reverse_iterator rit=categories->rbegin(); rit != categories->rend(); ++rit) {
		const ld::Atom* categoryMethodListAtom;
		if ( meta )
			categoryMethodListAtom = Category<A>::getClassMethods(state, *rit);
		else
			categoryMethodListAtom = Category<A>::getInstanceMethods(state, *rit);
		if ( categoryMethodListAtom != NULL ) {
			for (ld::Fixup::iterator fit=categoryMethodListAtom->fixupsBegin(); fit != categoryMethodListAtom->fixupsEnd(); ++fit) {
				ld::Fixup fixup = *fit;
				fixup.offsetInAtom += slide;
				_fixups.push_back(fixup);
				//if ( fixup.binding == ld::Fixup::bindingDirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, name=%s\n", fixup.offsetInAtom, fixup.u.target->name());
			}
			slide += 3*sizeof(pint_t) * MethodList<A>::count(state, categoryMethodListAtom);
		}
	}
	// add method list from base class last
	if ( baseMethodList != NULL ) {
		for (ld::Fixup::iterator fit=baseMethodList->fixupsBegin(); fit != baseMethodList->fixupsEnd(); ++fit) {
			ld::Fixup fixup = *fit;
			fixup.offsetInAtom += slide;
			_fixups.push_back(fixup);
		}
	}
}


template <typename A> 
ProtocolListAtom<A>::ProtocolListAtom(ld::Internal& state, const ld::Atom* baseProtocolList, 
									const std::vector<const ld::Atom*>* categories, std::set<const ld::Atom*>& deadAtoms)
  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
			symbolTableNotIn, false, false, false, ld::Atom::Alignment(3)), _file(NULL), _protocolCount(0) 
{
	unsigned int fixupCount = 0;
	if ( baseProtocolList != NULL ) {
		// if base class has protocol list, then associate new protocol list with file defining class
		_file = baseProtocolList->file();
		// calculate total size of merged protocol list
		_protocolCount = ProtocolList<A>::count(state, baseProtocolList);
		deadAtoms.insert(baseProtocolList);
		fixupCount = baseProtocolList->fixupsEnd() - baseProtocolList->fixupsBegin();
	}
	for (std::vector<const ld::Atom*>::const_iterator ait=categories->begin(); ait != categories->end(); ++ait) {
		const ld::Atom* categoryProtocolListAtom = Category<A>::getProtocols(state, *ait);
		if ( categoryProtocolListAtom != NULL ) {
			_protocolCount += ProtocolList<A>::count(state, categoryProtocolListAtom);
			fixupCount += (categoryProtocolListAtom->fixupsEnd() - categoryProtocolListAtom->fixupsBegin());
			deadAtoms.insert(categoryProtocolListAtom);
			// if base class did not have protocol list, associate new protocol list with file the defined category
			if ( _file == NULL )
				_file = categoryProtocolListAtom->file();
		}
	}
	//fprintf(stderr, "total merged protocol count=%u\n", _protocolCount);
	//fprintf(stderr, "total merged fixup count=%u\n", fixupCount);
	
	// copy fixups and adjust offsets 
	_fixups.reserve(fixupCount);
	uint32_t slide = 0;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryProtocolListAtom = Category<A>::getProtocols(state, *it);
		if ( categoryProtocolListAtom != NULL ) {
			for (ld::Fixup::iterator fit=categoryProtocolListAtom->fixupsBegin(); fit != categoryProtocolListAtom->fixupsEnd(); ++fit) {
				ld::Fixup fixup = *fit;
				fixup.offsetInAtom += slide;
				_fixups.push_back(fixup);
				//if ( fixup.binding == ld::Fixup::bindingDirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, name=%s\n", fixup.offsetInAtom, fixup.u.target->name());
			}
			slide += sizeof(pint_t) * ProtocolList<A>::count(state, categoryProtocolListAtom);
		}
	}
	// add method list from base class last
	if ( baseProtocolList != NULL ) {
		for (ld::Fixup::iterator fit=baseProtocolList->fixupsBegin(); fit != baseProtocolList->fixupsEnd(); ++fit) {
			ld::Fixup fixup = *fit;
			fixup.offsetInAtom += slide;
			_fixups.push_back(fixup);
		}
	}
}


template <typename A> 
PropertyListAtom<A>::PropertyListAtom(ld::Internal& state, const ld::Atom* basePropertyList, 
									const std::vector<const ld::Atom*>* categories, std::set<const ld::Atom*>& deadAtoms)
  : ld::Atom(_s_section, ld::Atom::definitionRegular, ld::Atom::combineNever,
			ld::Atom::scopeLinkageUnit, ld::Atom::typeUnclassified, 
			symbolTableNotIn, false, false, false, ld::Atom::Alignment(3)), _file(NULL), _propertyCount(0) 
{
	unsigned int fixupCount = 0;
	if ( basePropertyList != NULL ) {
		// if base class has property list, then associate new property list with file defining class
		_file = basePropertyList->file();
		// calculate total size of merged property list
		_propertyCount = PropertyList<A>::count(state, basePropertyList);
		deadAtoms.insert(basePropertyList);
		fixupCount = basePropertyList->fixupsEnd() - basePropertyList->fixupsBegin();
	}
	for (std::vector<const ld::Atom*>::const_iterator ait=categories->begin(); ait != categories->end(); ++ait) {
		const ld::Atom* categoryPropertyListAtom = Category<A>::getProperties(state, *ait);
		if ( categoryPropertyListAtom != NULL ) {
			_propertyCount += PropertyList<A>::count(state, categoryPropertyListAtom);
			fixupCount += (categoryPropertyListAtom->fixupsEnd() - categoryPropertyListAtom->fixupsBegin());
			deadAtoms.insert(categoryPropertyListAtom);
			// if base class did not have property list, associate new property list with file the defined category
			if ( _file == NULL )
				_file = categoryPropertyListAtom->file();
		}
	}
	//fprintf(stderr, "total merged property count=%u\n", _propertyCount);
	//fprintf(stderr, "total merged fixup count=%u\n", fixupCount);
	
	// copy fixups and adjust offsets 
	_fixups.reserve(fixupCount);
	uint32_t slide = 0;
	for (std::vector<const ld::Atom*>::const_iterator it=categories->begin(); it != categories->end(); ++it) {
		const ld::Atom* categoryPropertyListAtom = Category<A>::getProperties(state, *it);
		if ( categoryPropertyListAtom != NULL ) {
			for (ld::Fixup::iterator fit=categoryPropertyListAtom->fixupsBegin(); fit != categoryPropertyListAtom->fixupsEnd(); ++fit) {
				ld::Fixup fixup = *fit;
				fixup.offsetInAtom += slide;
				_fixups.push_back(fixup);
				//fprintf(stderr, "offset=0x%08X, binding=%d\n", fixup.offsetInAtom, fixup.binding);
				//if ( fixup.binding == ld::Fixup::bindingDirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, name=%s\n", fixup.offsetInAtom, fixup.u.target->name());
				//else if ( fixup.binding == ld::Fixup::bindingsIndirectlyBound )
				//	fprintf(stderr, "offset=0x%08X, indirect index=%u, name=%s\n", fixup.offsetInAtom, fixup.u.bindingIndex, 
				//			(char*)(state.indirectBindingTable[fixup.u.bindingIndex]->rawContentPointer()));
			}
			slide += 2*sizeof(pint_t) * PropertyList<A>::count(state, categoryPropertyListAtom);
		}
	}
	// add method list from base class last
	if ( basePropertyList != NULL ) {
		for (ld::Fixup::iterator fit=basePropertyList->fixupsBegin(); fit != basePropertyList->fixupsEnd(); ++fit) {
			ld::Fixup fixup = *fit;
			fixup.offsetInAtom += slide;
			_fixups.push_back(fixup);
		}
	}
}




void doPass(const Options& opts, ld::Internal& state)
{	
	// only make image info section if objc was used
	if ( state.objcObjectConstraint != ld::File::objcConstraintNone ) {

		// verify dylibs are GC compatible with object files
		if ( state.objcObjectConstraint != state.objcDylibConstraint ) {
			if (   (state.objcDylibConstraint == ld::File::objcConstraintRetainRelease)
				&& (state.objcObjectConstraint == ld::File::objcConstraintGC) ) {
					throw "Linked dylibs built for retain/release but object files built for GC-only";
			}
			else if (   (state.objcDylibConstraint == ld::File::objcConstraintGC)
				     && (state.objcObjectConstraint == ld::File::objcConstraintRetainRelease) ) {
					throw "Linked dylibs built for GC-only but object files built for retain/release";
			}
		}
	
		const bool compaction = opts.objcGcCompaction();
		
		// add image info atom
		switch ( opts.architecture() ) {
			case CPU_TYPE_X86_64:
				state.addAtom(*new ObjCImageInfoAtom<x86_64>(state.objcObjectConstraint, compaction, 
								state.hasObjcReplacementClasses, true));
				break;
			case CPU_TYPE_I386:
				state.addAtom(*new ObjCImageInfoAtom<x86>(state.objcObjectConstraint, compaction, 
							state.hasObjcReplacementClasses, opts.objCABIVersion2POverride() ? true : false));
				break;
			case CPU_TYPE_POWERPC:
				state.addAtom(*new ObjCImageInfoAtom<ppc>(state.objcObjectConstraint, compaction, 
							state.hasObjcReplacementClasses, false));
				break;
			case CPU_TYPE_ARM:
				state.addAtom(*new ObjCImageInfoAtom<arm>(state.objcObjectConstraint, compaction, 
							state.hasObjcReplacementClasses, true));
				break;
			case CPU_TYPE_POWERPC64:
				state.addAtom(*new ObjCImageInfoAtom<ppc64>(state.objcObjectConstraint, compaction, 
							state.hasObjcReplacementClasses, true));
				break;
			default:
				assert(0 && "unknown objc arch");
		}	
	}
	
	if ( opts.objcCategoryMerging() ) {
		// optimize classes defined in this linkage unit by merging in categories also in this linkage unit
		switch ( opts.architecture() ) {
			case CPU_TYPE_X86_64:
				OptimizeCategories<x86_64>::doit(opts, state);
				break;
			case CPU_TYPE_I386:
				// disable optimization until fully tested
				//if ( opts.objCABIVersion2POverride() )
				//	OptimizeCategories<x86>::doit(opts, state);
				break;
			case CPU_TYPE_ARM:
				// disable optimization until fully tested
				//OptimizeCategories<arm>::doit(opts, state);
				break;
			case CPU_TYPE_POWERPC64:
			case CPU_TYPE_POWERPC:
				break;
			default:
				assert(0 && "unknown objc arch");
		}	
	}
}


} // namespace objc
} // namespace passes 
} // namespace ld 
