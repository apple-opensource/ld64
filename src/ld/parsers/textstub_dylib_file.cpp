/* -*- mode: C++; c-basic-offset: 4; tab-width: 4 -*-
 *
 * Copyright (c) 2015 Apple Inc. All rights reserved.
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


#include <sys/param.h>
#include <sys/mman.h>
#include <tapi/tapi.h>
#include <vector>

#include "Architectures.hpp"
#include "Bitcode.hpp"
#include "MachOFileAbstraction.hpp"
#include "MachOTrie.hpp"
#include "generic_dylib_file.hpp"
#include "textstub_dylib_file.hpp"


namespace textstub {
namespace dylib {

//
// The reader for a dylib extracts all exported symbols names from the memory-mapped
// dylib, builds a hash table, then unmaps the file.  This is an important memory
// savings for large dylibs.
//
template <typename A>
class File final : public generic::dylib::File<A>
{
	using Base = generic::dylib::File<A>;

public:
					File(const char* path, const uint8_t* fileContent, uint64_t fileLength, const Options *opts,
						 time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace,
						 bool linkingMainExecutable, bool hoistImplicitPublicDylibs,
						 const ld::VersionSet& platforms, bool allowWeakImports,
						 cpu_type_t cpuType, cpu_subtype_t cpuSubType, bool enforceDylibSubtypesMatch,
						 bool allowSimToMacOSX, bool addVers, bool buildingForSimulator,
						 bool logAllFiles, const char* installPath, bool indirectDylib,
					         bool ignoreMismatchPlatform, bool usingBitcode);
					File(tapi::LinkerInterfaceFile* file, const char *path, const Options *opts,
						 time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace,
						 bool linkingMainExecutable, bool hoistImplicitPublicDylibs,
						 const ld::VersionSet& platforms, bool allowWeakImports,
						 cpu_type_t cpuType, cpu_subtype_t cpuSubType, bool enforceDylibSubtypesMatch,
						 bool allowSimToMacOSX, bool addVers, bool buildingForSimulator,
						 bool logAllFiles, const char* installPath, bool indirectDylib,
					         bool ignoreMismatchPlatform, bool usingBitcode);
	virtual			~File() noexcept {}
	
	// overrides of generic::dylib::File
	virtual void	processIndirectLibraries(ld::dylib::File::DylibHandler*, bool addImplicitDylibs) override final;

private:
	void				init(tapi::LinkerInterfaceFile* file, const Options *opts, bool buildingForSimulator,
									 bool indirectDylib, bool linkingFlatNamespace, bool linkingMainExecutable,
									 const char *path, const ld::VersionSet& platforms, const char *targetInstallPath,
									 bool ignoreMismatchPlatform, bool usingBitcode);
	void				buildExportHashTable(const tapi::LinkerInterfaceFile* file);
	static bool useSimulatorVariant();
	
	const Options* _opts;
	tapi::LinkerInterfaceFile* _interface;
};

template <> bool File<x86>::useSimulatorVariant() { return true; }
template <> bool File<x86_64>::useSimulatorVariant() { return true; }
template <typename A> bool File<A>::useSimulatorVariant() { return false; }


static ld::VersionSet mapPlatform(tapi::Platform platform, bool useSimulatorVariant) {
	ld::VersionSet platforms;
	switch (platform) {
	case tapi::Platform::Unknown:
		break;
	case tapi::Platform::OSX:
		platforms.add({ld::kPlatform_macOS, 0});
		break;
	case tapi::Platform::iOS:
		if (useSimulatorVariant)
			platforms.add({ld::kPlatform_iOSSimulator, 0});
		else
			platforms.add({ld::kPlatform_iOS, 0});
		break;
	case tapi::Platform::watchOS:
		if (useSimulatorVariant)
			platforms.add({ld::kPlatform_watchOSSimulator, 0});
		else
			platforms.add({ld::kPlatform_watchOS, 0});
		break;
	case tapi::Platform::tvOS:
		if (useSimulatorVariant)
			platforms.add({ld::kPlatform_tvOSSimulator, 0});
		else
			platforms.add({ld::kPlatform_tvOS, 0});
		break;
	#if ((TAPI_API_VERSION_MAJOR == 1 &&  TAPI_API_VERSION_MINOR >= 2) || (TAPI_API_VERSION_MAJOR > 1))
	case tapi::Platform::bridgeOS:
		platforms.add({ld::kPlatform_bridgeOS, 0});
		break;
	#endif
	#if ((TAPI_API_VERSION_MAJOR == 1 &&  TAPI_API_VERSION_MINOR >= 4) || (TAPI_API_VERSION_MAJOR > 1))
	case tapi::Platform::iOSMac:
		platforms.add({ld::kPlatform_iOSMac, 0});
		break;
	case tapi::Platform::zippered:
		platforms.add({ld::kPlatform_macOS, 0});
		platforms.add({ld::kPlatform_iOSMac, 0});
		break;
	#endif
	}

	return platforms;
}

template <typename A>
File<A>::File(const char* path, const uint8_t* fileContent, uint64_t fileLength, const Options *opts,
		  time_t mTime, ld::File::Ordinal ord, bool linkingFlatNamespace,
		  bool linkingMainExecutable, bool hoistImplicitPublicDylibs, const ld::VersionSet& platforms,
		  bool allowWeakImports, cpu_type_t cpuType, cpu_subtype_t cpuSubType,
		  bool enforceDylibSubtypesMatch, bool allowSimToMacOSX, bool addVers,
		  bool buildingForSimulator, bool logAllFiles, const char* targetInstallPath,
		  bool indirectDylib, bool ignoreMismatchPlatform, bool usingBitcode)
: Base(strdup(path), mTime, ord, platforms, allowWeakImports, linkingFlatNamespace,
	   hoistImplicitPublicDylibs, allowSimToMacOSX, addVers)
{
	std::unique_ptr<tapi::LinkerInterfaceFile> file;
	std::string errorMessage;
	__block uint32_t linkMinOSVersion = 0;
	//FIXME handle this correctly once we have multi-platfrom TAPI
	platforms.forEach(^(ld::Platform platform, uint32_t version, bool &stop) {
		if (linkMinOSVersion == 0)
			linkMinOSVersion = version;
		if (platform == ld::kPlatform_macOS)
			linkMinOSVersion = version;
	});

// <rdar://problem/29038544> Support $ld$weak symbols in .tbd files
#if ((TAPI_API_VERSION_MAJOR == 1 &&  TAPI_API_VERSION_MINOR >= 3) || (TAPI_API_VERSION_MAJOR > 1))
	// Check if the library supports the new create API.
	if (tapi::APIVersion::isAtLeast(1, 3)) {
		tapi::ParsingFlags flags = tapi::ParsingFlags::None;
		if (enforceDylibSubtypesMatch)
			flags |= tapi::ParsingFlags::ExactCpuSubType;

		if (!allowWeakImports)
			flags |= tapi::ParsingFlags::DisallowWeakImports;

		_interface = tapi::LinkerInterfaceFile::create(
			path, cpuType, cpuSubType, flags,
			tapi::PackedVersion32(linkMinOSVersion), errorMessage);
	} else
#endif
#if ((TAPI_API_VERSION_MAJOR == 1 &&  TAPI_API_VERSION_MINOR >= 1) || (TAPI_API_VERSION_MAJOR > 1))
	if (tapi::APIVersion::isAtLeast(1, 1)) {
		tapi::ParsingFlags flags = tapi::ParsingFlags::None;
		if (enforceDylibSubtypesMatch)
			flags |= tapi::ParsingFlags::ExactCpuSubType;

		if (!allowWeakImports)
			flags |= tapi::ParsingFlags::DisallowWeakImports;

		_interface = tapi::LinkerInterfaceFile::create(
			path, fileContent, fileLength, cpuType, cpuSubType, flags,
			tapi::PackedVersion32(linkMinOSVersion), errorMessage);
	} else
#endif
#if (TAPI_API_VERSION_MAJOR >= 1)
	{
		auto matchingType = enforceDylibSubtypesMatch ?
			tapi::CpuSubTypeMatching::Exact : tapi::CpuSubTypeMatching::ABI_Compatible;

		_interface = tapi::LinkerInterfaceFile::create(
			path, fileContent, fileLength, cpuType, cpuSubType, matchingType,
			tapi::PackedVersion32(linkMinOSVersion), errorMessage);
	}
#endif

	if (!_interface)
		throw strdup(errorMessage.c_str());

	// unmap file - it is no longer needed.
	munmap((caddr_t)fileContent, fileLength);

	// write out path for -t option
	if ( logAllFiles )
		printf("%s\n", path);

	init(_interface, opts, buildingForSimulator, indirectDylib, linkingFlatNamespace,
		 linkingMainExecutable, path, platforms, targetInstallPath, ignoreMismatchPlatform, usingBitcode);
}

	template<typename A>
	File<A>::File(tapi::LinkerInterfaceFile* file, const char* path, const Options *opts,
				  time_t mTime, ld::File::Ordinal ordinal, bool linkingFlatNamespace,
				 bool linkingMainExecutable, bool hoistImplicitPublicDylibs,
				 const ld::VersionSet& platforms, bool allowWeakImports,
				 cpu_type_t cpuType, cpu_subtype_t cpuSubType, bool enforceDylibSubtypesMatch,
				 bool allowSimToMacOSX, bool addVers, bool buildingForSimulator,
				 bool logAllFiles, const char* installPath, bool indirectDylib,
				 bool ignoreMismatchPlatform, bool usingBitcode)
	: Base(strdup(path), mTime, ordinal, platforms, allowWeakImports, linkingFlatNamespace,
		   hoistImplicitPublicDylibs, allowSimToMacOSX, addVers), _interface(file)
{
	init(_interface, opts, buildingForSimulator, indirectDylib, linkingFlatNamespace,
		 linkingMainExecutable, path, platforms, installPath, ignoreMismatchPlatform, usingBitcode);
}
	
template<typename A>
void File<A>::init(tapi::LinkerInterfaceFile* file, const Options *opts, bool buildingForSimulator,
				   bool indirectDylib, bool linkingFlatNamespace, bool linkingMainExecutable,
				   const char *path, const ld::VersionSet& platforms, const char *targetInstallPath,
				   bool ignoreMismatchPlatform, bool usingBitcode) {
	_opts = opts;
	this->_bitcode = std::unique_ptr<ld::Bitcode>(new ld::Bitcode(nullptr, 0));
	this->_noRexports = !file->hasReexportedLibraries();
	this->_hasWeakExports = file->hasWeakDefinedExports();
	this->_dylibInstallPath = strdup(file->getInstallName().c_str());
	this->_installPathOverride = file->isInstallNameVersionSpecific();
	this->_dylibCurrentVersion = file->getCurrentVersion();
	this->_dylibCompatibilityVersion = file->getCompatibilityVersion();
	this->_swiftVersion = file->getSwiftVersion();
	this->_parentUmbrella = file->getParentFrameworkName().empty() ? nullptr : strdup(file->getParentFrameworkName().c_str());
	this->_appExtensionSafe = file->isApplicationExtensionSafe();

	// if framework, capture framework name
	const char* lastSlash = strrchr(this->_dylibInstallPath, '/');
	if ( lastSlash != NULL ) {
		const char* leafName = lastSlash+1;
		char frname[strlen(leafName)+32];
		strcpy(frname, leafName);
		strcat(frname, ".framework/");

		if ( strstr(this->_dylibInstallPath, frname) != NULL )
			this->_frameworkName = leafName;
	}
	
	for (auto &client : file->allowableClients())
		this->_allowableClients.push_back(strdup(client.c_str()));
	
	// <rdar://problem/20659505> [TAPI] Don't hoist "public" (in /usr/lib/) dylibs that should not be directly linked
	this->_hasPublicInstallName = file->hasAllowableClients() ? false : this->isPublicLocation(file->getInstallName().c_str());
	
	for (const auto &client : file->allowableClients())
		this->_allowableClients.emplace_back(strdup(client.c_str()));

	ld::VersionSet lcPlatforms = mapPlatform(file->getPlatform(), useSimulatorVariant());
	this->_platforms = lcPlatforms;

	// check cross-linking
	platforms.forEach(^(ld::Platform platform, uint32_t version, bool &stop) {
		if (!lcPlatforms.contains(platform) ) {
			this->_wrongOS = true;
			if ( this->_addVersionLoadCommand && !indirectDylib && !ignoreMismatchPlatform ) {
				if (buildingForSimulator && !this->_allowSimToMacOSXLinking) {
					if ( usingBitcode )
						throwf("building for %s simulator, but linking against dylib built for %s,",
									 platforms.to_str().c_str(), lcPlatforms.to_str().c_str());
					else
						warning("URGENT: building for %s simulator, but linking against dylib (%s) built for %s. "
										"Note: This will be an error in the future.",
										platforms.to_str().c_str(), path, lcPlatforms.to_str().c_str());
				}
			} else {
				if ( usingBitcode )
					throwf("building for %s, but linking against dylib built for %s,",
								 platforms.to_str().c_str(), lcPlatforms.to_str().c_str());
				else if ( (getenv("RC_XBS") != NULL) && (getenv("RC_BUILDIT") == NULL) ) // FIXME: remove after platform bringup
					warning("URGENT: building for %s, but linking against dylib (%s) built for %s. "
									"Note: This will be an error in the future.",
									platforms.to_str().c_str(), path, lcPlatforms.to_str().c_str());
			}
		}
	});
	
	for (const auto& reexport : file->reexportedLibraries()) {
		const char *path = strdup(reexport.c_str());
		if ( (targetInstallPath == nullptr) || (strcmp(targetInstallPath, path) != 0) )
			this->_dependentDylibs.emplace_back(path, true);
	}
	
	for (const auto& symbol : file->ignoreExports())
		this->_ignoreExports.insert(strdup(symbol.c_str()));
	
	// if linking flat and this is a flat dylib, create one atom that references all imported symbols.
	if ( linkingFlatNamespace && linkingMainExecutable && (file->hasTwoLevelNamespace() == false) ) {
		std::vector<const char*> importNames;
		importNames.reserve(file->undefineds().size());
		// We do not need to strdup the name, because that will be done by the
		// ImportAtom constructor.
		for (const auto &sym : file->undefineds())
			importNames.emplace_back(sym.getName().c_str());
		this->_importAtom = new generic::dylib::ImportAtom<A>(*this, importNames);
	}
	
	// build hash table
	buildExportHashTable(file);
}

template <typename A>
void File<A>::buildExportHashTable(const tapi::LinkerInterfaceFile* file) {
	if (this->_s_logHashtable )
		fprintf(stderr, "ld: building hashtable from text-stub info in %s\n", this->path());

	for (const auto &sym : file->exports()) {
		const char* name = sym.getName().c_str();
		bool weakDef = sym.isWeakDefined();
		bool tlv = sym.isThreadLocalValue();

		typename Base::AtomAndWeak bucket = { nullptr, weakDef, tlv, 0 };
		if ( this->_s_logHashtable )
			fprintf(stderr, "  adding %s to hash table for %s\n", name, this->path());
		this->_atoms[strdup(name)] = bucket;
	}
}

template <typename A>
void File<A>::processIndirectLibraries(ld::dylib::File::DylibHandler* handler, bool addImplicitDylibs) {
	if (_interface)
		_opts->addTAPIInterface(_interface, this->path());
	Base::processIndirectLibraries(handler, addImplicitDylibs);
}

template <typename A>
class Parser
{
public:
	using P = typename A::P;

	static ld::dylib::File*	parse(const char* path, const uint8_t* fileContent,
								  uint64_t fileLength, time_t mTime,
								  ld::File::Ordinal ordinal, const Options& opts,
								  bool indirectDylib, cpu_type_t architecture, cpu_subtype_t subArchitecture)
	{
		return new File<A>(path, fileContent, fileLength, &opts, mTime, ordinal,
						   opts.flatNamespace(),
						   opts.linkingMainExecutable(),
						   opts.implicitlyLinkIndirectPublicDylibs(),
						   opts.platforms(),
						   opts.allowWeakImports(),
						   architecture,
						   subArchitecture,
						   opts.enforceDylibSubtypesMatch(),
						   opts.allowSimulatorToLinkWithMacOSX(),
						   opts.addVersionLoadCommand(),
						   opts.targetIOSSimulator(),
						   opts.logAllFiles(),
						   opts.installPath(),
						   indirectDylib,
						   opts.outputKind() == Options::kPreload,
				   		   opts.bundleBitcode());
	}
	
	static ld::dylib::File*	parse(const char* path, tapi::LinkerInterfaceFile* file, time_t mTime,
								  ld::File::Ordinal ordinal, const Options& opts,
								  bool indirectDylib, cpu_type_t architecture, cpu_subtype_t subArchitecture)
	{
		return new File<A>(file, path, &opts, mTime, ordinal,
						   opts.flatNamespace(),
						   opts.linkingMainExecutable(),
						   opts.implicitlyLinkIndirectPublicDylibs(),
						   opts.platforms(),
						   opts.allowWeakImports(),
						   architecture,
						   subArchitecture,
						   opts.enforceDylibSubtypesMatch(),
						   opts.allowSimulatorToLinkWithMacOSX(),
						   opts.addVersionLoadCommand(),
						   opts.targetIOSSimulator(),
						   opts.logAllFiles(),
						   opts.installPath(),
						   indirectDylib,
						   opts.outputKind() == Options::kPreload,
						   opts.bundleBitcode());
	}

};


static ld::dylib::File* parseAsArchitecture(const uint8_t* fileContent, uint64_t fileLength, const char* path,
											time_t modTime, ld::File::Ordinal ordinal, const Options& opts,
											bool bundleLoader, bool indirectDylib,
											cpu_type_t architecture, cpu_subtype_t subArchitecture)
{
	switch ( architecture ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			return Parser<x86_64>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			return Parser<x86>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			return Parser<arm>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
			return Parser<arm64>::parse(path, fileContent, fileLength, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
		default:
			throwf("unsupported architecture for tbd file");
	}
	assert(0 && "function should return valid pointer or throw");
}


static ld::dylib::File *parseAsArchitecture(const char *path, tapi::LinkerInterfaceFile* file, time_t modTime,
																						ld::File::Ordinal ordinal, const Options& opts, bool indirectDylib,
																						cpu_type_t architecture, cpu_subtype_t subArchitecture)
{
	switch ( architecture ) {
#if SUPPORT_ARCH_x86_64
		case CPU_TYPE_X86_64:
			return Parser<x86_64>::parse(path, file, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
#if SUPPORT_ARCH_i386
		case CPU_TYPE_I386:
			return Parser<x86>::parse(path, file, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
#if SUPPORT_ARCH_arm_any
		case CPU_TYPE_ARM:
			return Parser<arm>::parse(path, file, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
#if SUPPORT_ARCH_arm64
		case CPU_TYPE_ARM64:
			return Parser<arm64>::parse(path, file, modTime, ordinal, opts, indirectDylib, architecture, subArchitecture);
#endif
		default:
			throwf("unsupported architecture for tbd file");
	}
	assert(0 && "function should return valid pointer or throw");
}


//
// main function used by linker to instantiate ld::Files
//
ld::dylib::File* parse(const uint8_t* fileContent, uint64_t fileLength, const char* path,
					   time_t modtime, const Options& opts, ld::File::Ordinal ordinal,
					   bool bundleLoader, bool indirectDylib)
{
	if (!tapi::LinkerInterfaceFile::isSupported(path, fileContent, fileLength))
		return nullptr;

	try {
		return parseAsArchitecture(fileContent, fileLength, path, modtime, ordinal, opts, bundleLoader, indirectDylib, opts.architecture(), opts.subArchitecture());
	} catch (...) {
		if (!opts.fallbackArchitecture())
			throw;
	}

	warning("architecture %s not present in TBD %s, attempting fallback", opts.architectureName(), path);
	return parseAsArchitecture(fileContent, fileLength, path, modtime, ordinal, opts, bundleLoader, indirectDylib, opts.fallbackArchitecture(), opts.fallbackSubArchitecture());
}

ld::dylib::File *parse(const char *path, tapi::LinkerInterfaceFile* file, time_t modTime,
					   ld::File::Ordinal ordinal, const Options& opts, bool indirectDylib) {
	try {
		return parseAsArchitecture(path, file, modTime, ordinal, opts, indirectDylib, opts.architecture(), opts.subArchitecture());
	} catch (...) {
		if (!opts.fallbackArchitecture())
			throw;
	}

	warning("architecture %s not present in TBD %s, attempting fallback", opts.architectureName(), path);
	return parseAsArchitecture(path, file, modTime, ordinal, opts, indirectDylib, opts.fallbackArchitecture(), opts.fallbackSubArchitecture());
}

} // namespace dylib
} // namespace textstub
