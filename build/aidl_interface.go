// Copyright (C) 2018 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

package aidl

import (
	"fmt"
	"io"
	"path/filepath"
	"strconv"
	"strings"
	"sync"

	"github.com/google/blueprint"
	"github.com/google/blueprint/pathtools"
	"github.com/google/blueprint/proptools"

	"android/soong/android"
	"android/soong/cc"
	"android/soong/genrule"
	"android/soong/java"
	"android/soong/phony"
)

var (
	aidlInterfaceSuffix = "_interface"
	aidlApiSuffix       = "-api"
	langCpp             = "cpp"
	langJava            = "java"

	pctx = android.NewPackageContext("android/aidl")

	aidlCppRule = pctx.StaticRule("aidlCppRule", blueprint.RuleParams{
		Command: `rm -rf "${outDir}" && ` +
			`mkdir -p "${outDir}" "${headerDir}" && ` +
			`${aidlCmd} --lang=cpp --structured --ninja -d ${out}.d ` +
			`-h ${headerDir} -o ${outDir} ${imports} ${in}`,
		Depfile:     "${out}.d",
		Deps:        blueprint.DepsGCC,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL CPP ${in}",
	}, "imports", "headerDir", "outDir")

	aidlJavaRule = pctx.StaticRule("aidlJavaRule", blueprint.RuleParams{
		Command: `rm -rf "${outDir}" && mkdir -p "${outDir}" && ` +
			`${aidlCmd} --lang=java --structured --ninja -d ${out}.d ` +
			`-o ${outDir} ${imports} ${in}`,
		Depfile:     "${out}.d",
		Deps:        blueprint.DepsGCC,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL Java ${in}",
	}, "imports", "outDir")

	aidlDumpApiRule = pctx.StaticRule("aidlDumpApiRule", blueprint.RuleParams{
		Command: `rm -rf "${out}" && mkdir -p "${out}" && ` +
			`${aidlCmd} --dumpapi --structured ${imports} --out ${out} ${in}`,
		CommandDeps: []string{"${aidlCmd}"},
	}, "imports")

	aidlFreezeApiRule = pctx.AndroidStaticRule("aidlFreezeApiRule",
		blueprint.RuleParams{
			Command: `rm -rf ${to}/* && ` +
				`cp -rf ${in}/* ${to} && touch ${out}`,
		}, "to")

	aidlCheckApiRule = pctx.StaticRule("aidlCheckApiRule", blueprint.RuleParams{
		Command:     `${aidlCmd} --checkapi ${old} ${new} && touch ${out}`,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL CHECK API: ${new} against ${old}",
	}, "old", "new")
)

func init() {
	pctx.HostBinToolVariable("aidlCmd", "aidl")
	android.RegisterModuleType("aidl_interface", aidlInterfaceFactory)
}

// wrap(p, a, s) = [p + v + s for v in a]
func wrap(prefix string, strs []string, suffix string) []string {
	ret := make([]string, len(strs))
	for i, v := range strs {
		ret[i] = prefix + v + suffix
	}
	return ret
}

// concat(a...) = sum((i for i in a), [])
func concat(sstrs ...[]string) []string {
	var ret []string
	for _, v := range sstrs {
		ret = append(ret, v...)
	}
	return ret
}

func isRelativePath(path string) bool {
	if path == "" {
		return true
	}
	return filepath.Clean(path) == path && path != ".." &&
		!strings.HasPrefix(path, "../") && !strings.HasPrefix(path, "/")
}

type aidlGenProperties struct {
	Input    string // a single aidl file
	AidlRoot string // base directory for the input aidl file
	Imports  []string
	Lang     string // target language [java|cpp]
	BaseName string
}

type aidlGenRule struct {
	android.ModuleBase

	properties aidlGenProperties

	genHeaderDir android.ModuleGenPath
	genOutputs   android.WritablePaths
}

var _ android.SourceFileProducer = (*aidlGenRule)(nil)
var _ genrule.SourceFileGenerator = (*aidlGenRule)(nil)

func (g *aidlGenRule) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	// g.properties.Input = some_dir/pkg/to/IFoo.aidl
	// g.properties.AidlRoot = some_dir
	// input = <module_root>/some_dir/pkg/to/IFoo.aidl
	// outDir = out/soong/.intermediate/..../gen/
	// outFile = out/soong/.intermediates/..../gen/pkg/to/IFoo.{java|cpp}
	input := android.PathForModuleSrc(ctx, g.properties.Input).WithSubDir(ctx, g.properties.AidlRoot)
	outDir := android.PathForModuleGen(ctx)
	var outFile android.WritablePath
	if g.properties.Lang == langJava {
		outFile = android.PathForModuleGen(ctx, pathtools.ReplaceExtension(input.Rel(), "java"))
	} else {
		outFile = android.PathForModuleGen(ctx, pathtools.ReplaceExtension(input.Rel(), "cpp"))
	}
	g.genOutputs = []android.WritablePath{outFile}

	var importPaths []string
	var checkApiTimestamps android.Paths
	ctx.VisitDirectDeps(func(dep android.Module) {
		if importedAidl, ok := dep.(*aidlInterface); ok {
			importPaths = append(importPaths, importedAidl.properties.Full_import_path)
		} else if api, ok := dep.(*aidlApi); ok {
			// When compiling an AIDL interface, also make sure that each
			// version of the interface is compatible with its previous version
			for _, path := range api.checkApiTimestamps {
				checkApiTimestamps = append(checkApiTimestamps, path)
			}
		}
	})

	imports := strings.Join(wrap("-I", importPaths, ""), " ")

	if g.properties.Lang == langJava {
		ctx.ModuleBuild(pctx, android.ModuleBuildParams{
			Rule:      aidlJavaRule,
			Input:     input,
			Implicits: checkApiTimestamps,
			Outputs:   g.genOutputs,
			Args: map[string]string{
				"imports": imports,
				"outDir":  outDir.String(),
			},
		})
	} else {
		g.genHeaderDir = android.PathForModuleGen(ctx, "include")
		typeName := strings.TrimSuffix(filepath.Base(input.Rel()), ".aidl")
		packagePath := filepath.Dir(input.Rel())
		baseName := typeName
		// TODO(b/111362593): generate_cpp.cpp uses heuristics to figure out if
		//   an interface name has a leading I. Those same heuristics have been
		//   moved here.
		if len(baseName) >= 2 && baseName[0] == 'I' &&
			strings.ToUpper(baseName)[1] == baseName[1] {
			baseName = strings.TrimPrefix(typeName, "I")
		}
		var headers android.WritablePaths
		headers = append(headers, g.genHeaderDir.Join(ctx, packagePath,
			typeName+".h"))
		headers = append(headers, g.genHeaderDir.Join(ctx, packagePath,
			"Bp"+baseName+".h"))
		headers = append(headers, g.genHeaderDir.Join(ctx, packagePath,
			"Bn"+baseName+".h"))
		ctx.ModuleBuild(pctx, android.ModuleBuildParams{
			Rule:            aidlCppRule,
			Input:           input,
			Implicits:       checkApiTimestamps,
			Outputs:         g.genOutputs,
			ImplicitOutputs: headers,
			Args: map[string]string{
				"imports":   imports,
				"headerDir": g.genHeaderDir.String(),
				"outDir":    outDir.String(),
			},
		})
	}
}

func (g *aidlGenRule) GeneratedSourceFiles() android.Paths {
	return g.genOutputs.Paths()
}

func (g *aidlGenRule) Srcs() android.Paths {
	return g.genOutputs.Paths()
}

func (g *aidlGenRule) GeneratedDeps() android.Paths {
	return g.genOutputs.Paths()
}

func (g *aidlGenRule) GeneratedHeaderDirs() android.Paths {
	return android.Paths{g.genHeaderDir}
}

func (g *aidlGenRule) DepsMutator(ctx android.BottomUpMutatorContext) {
	ctx.AddDependency(ctx.Module(), nil, wrap("", g.properties.Imports, aidlInterfaceSuffix)...)
	ctx.AddDependency(ctx.Module(), nil, g.properties.BaseName+aidlApiSuffix)
}

func aidlGenFactory() android.Module {
	g := &aidlGenRule{}
	g.AddProperties(&g.properties)
	android.InitAndroidModule(g)
	return g
}

type aidlApiProperties struct {
	BaseName string
	Inputs   []string
	Imports  []string
	Api_dir  *string
	Versions []string
	AidlRoot string // base directory for the input aidl file
}

type aidlApi struct {
	android.ModuleBase

	properties aidlApiProperties

	// for triggering api check for version X against version X-1
	checkApiTimestamps android.WritablePaths

	// for triggering freezing API as the new version
	freezeApiTimestamp android.WritablePath
}

func (m *aidlApi) apiDir() string {
	if m.properties.Api_dir != nil {
		return *(m.properties.Api_dir)
	} else {
		return "api"
	}
}

func (m *aidlApi) latestVersion() string {
	if len(m.properties.Versions) == 0 {
		return ""
	} else {
		return m.properties.Versions[len(m.properties.Versions)-1]
	}
}

func (m *aidlApi) createApiDumpFromSource(ctx android.ModuleContext) (apiDir android.WritablePath, apiFiles android.WritablePaths) {
	var importPaths []string
	ctx.VisitDirectDeps(func(dep android.Module) {
		if importedAidl, ok := dep.(*aidlInterface); ok {
			importPaths = append(importPaths, importedAidl.properties.Full_import_path)
		}
	})

	var srcs android.Paths
	for _, input := range m.properties.Inputs {
		srcs = append(srcs, android.PathForModuleSrc(ctx, input).WithSubDir(
			ctx, m.properties.AidlRoot))
	}

	apiDir = android.PathForModuleOut(ctx, "dump")
	for _, src := range srcs {
		apiFiles = append(apiFiles, android.PathForModuleOut(ctx, "dump", src.Rel()))
	}
	imports := strings.Join(wrap("-I", importPaths, ""), " ")
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:            aidlDumpApiRule,
		Inputs:          srcs,
		Output:          apiDir,
		ImplicitOutputs: apiFiles,
		Args: map[string]string{
			"imports": imports,
		},
	})
	return apiDir, apiFiles
}

func (m *aidlApi) freezeApiDumpAsVersion(ctx android.ModuleContext, apiDumpDir android.Path, apiFiles android.Paths, version string) android.WritablePath {
	timestampFile := android.PathForModuleOut(ctx, "freezeapi_"+version+".timestamp")
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:        aidlFreezeApiRule,
		Description: "Freezing AIDL API of " + m.properties.BaseName + " as version " + version,
		Input:       apiDumpDir,
		Implicits:   apiFiles,
		Output:      timestampFile,
		Args: map[string]string{
			"to": android.PathForModuleSrc(ctx, m.apiDir(), version).String(),
		},
	})
	return timestampFile
}

func (m *aidlApi) checkCompatibility(ctx android.ModuleContext, oldApiDir android.Path, oldApiFiles android.Paths, newApiDir android.Path, newApiFiles android.Paths) android.WritablePath {
	newVersion := newApiDir.Base()
	timestampFile := android.PathForModuleOut(ctx, "checkapi_"+newVersion+".timestamp")
	var allApiFiles android.Paths
	allApiFiles = append(allApiFiles, oldApiFiles...)
	allApiFiles = append(allApiFiles, newApiFiles...)
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:      aidlCheckApiRule,
		Implicits: allApiFiles,
		Output:    timestampFile,
		Args: map[string]string{
			"old": oldApiDir.String(),
			"new": newApiDir.String(),
		},
	})
	return timestampFile
}

func (m *aidlApi) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	if len(m.properties.Versions) == 0 {
		return
	}

	apiDirs := make(map[string]android.ModuleSrcPath)
	apiFiles := make(map[string]android.Paths)
	for _, ver := range m.properties.Versions {
		apiDirs[ver] = android.PathForModuleSrc(ctx, m.apiDir(), ver)
		apiFiles[ver] = ctx.Glob(apiDirs[ver].Join(ctx, "**/*.aidl").String(), nil)
	}

	latestVersion := m.latestVersion()
	isLatestVersionEmpty := len(apiFiles[latestVersion]) == 0

	// Check that version X is backward compatible with version X-1
	// If the directory for the latest version is empty, it means the we are
	// in the process of creating a new version. Dump an API from the source
	// code and freezing it by putting it to the empty directory.
	for i, ver := range m.properties.Versions {
		if ver == latestVersion && isLatestVersionEmpty {
			apiDumpDir, apiFiles := m.createApiDumpFromSource(ctx)
			m.freezeApiTimestamp = m.freezeApiDumpAsVersion(ctx, apiDumpDir, apiFiles.Paths(), latestVersion)
		} else if i != 0 {
			oldVersion := m.properties.Versions[i-1]
			newVersion := m.properties.Versions[i]
			checkApiTimestamp := m.checkCompatibility(ctx, apiDirs[oldVersion], apiFiles[oldVersion], apiDirs[newVersion], apiFiles[newVersion])
			m.checkApiTimestamps = append(m.checkApiTimestamps, checkApiTimestamp)
		}
	}
}

func (m *aidlApi) AndroidMk() android.AndroidMkData {
	return android.AndroidMkData{
		Custom: func(w io.Writer, name, prefix, moduleDir string, data android.AndroidMkData) {
			android.WriteAndroidMkData(w, data)
			targetName := m.properties.BaseName + "-freeze-api"
			fmt.Fprintln(w, ".PHONY:", targetName)
			if m.freezeApiTimestamp != nil {
				fmt.Fprintln(w, targetName+":", m.freezeApiTimestamp.String())
			} else {
				fmt.Fprintln(w, targetName+":")
				if m.latestVersion() == "" {
					fmt.Fprintln(w, "\t@echo Directory to freeze API into is not specified. Use versions property to add.")
				} else {
					fmt.Fprintln(w, "\t@echo Can not freeze API because "+
						filepath.Join(moduleDir, m.apiDir(), m.latestVersion())+
						" already has the frozen API dump. Create a new version.")
				}
				fmt.Fprintln(w, "\t@exit 1")
			}
		},
	}
}

func (m *aidlApi) DepsMutator(ctx android.BottomUpMutatorContext) {
	ctx.AddDependency(ctx.Module(), nil, wrap("", m.properties.Imports, aidlInterfaceSuffix)...)
}

func aidlApiFactory() android.Module {
	m := &aidlApi{}
	m.AddProperties(&m.properties)
	android.InitAndroidModule(m)
	return m
}

type aidlInterfaceProperties struct {
	// Vndk properties for interface library only.
	cc.VndkProperties

	// Whether the library can be installed on the vendor image.
	Vendor_available *bool

	// Relative path for includes. By default assumes AIDL path is relative to current directory.
	// TODO(b/111117220): automatically compute by letting AIDL parse multiple files simultaneously
	Local_include_dir string

	// The owner of the module
	Owner *string

	// List of .aidl files which compose this interface.
	Srcs []string

	Imports []string

	// Whether to generate cpp.
	// Default: true
	Gen_cpp *bool

	// Used by gen dependency to fill out aidl include path
	Full_import_path string `blueprint:"mutated"`

	// Directory where API dumps are. Default is "api".
	Api_dir *string

	// Previous API versions that are now frozen. The version that is last in
	// the list is considered as the most recent version.
	Versions []string
}

type aidlInterface struct {
	android.ModuleBase

	properties aidlInterfaceProperties

	// For a corresponding .aidl source, example: "IFoo"
	types []string

	// For a corresponding .aidl source, example: "some/package/path"
	packagePaths []string
}

func (i *aidlInterface) shouldGenerateCpp() bool {
	// explicitly true if not specified to give early warning to devs
	return i.properties.Gen_cpp == nil || *i.properties.Gen_cpp
}

func (i *aidlInterface) checkAndUpdateSources(mctx android.LoadHookContext) {
	if len(i.properties.Srcs) == 0 {
		mctx.PropertyErrorf("srcs", "No sources provided.")
	}

	for _, source := range i.properties.Srcs {
		if !strings.HasSuffix(source, ".aidl") {
			mctx.PropertyErrorf("srcs", "Source must be a .aidl file: "+source)
			continue
		}

		name := strings.TrimSuffix(source, ".aidl")
		i.types = append(i.types, filepath.Base(name))

		relativePath, err := filepath.Rel(i.properties.Local_include_dir, source)
		if err != nil || !isRelativePath(relativePath) {
			mctx.PropertyErrorf("srcs", "Source is not in local_include_dir: "+source)
		}
		i.packagePaths = append(i.packagePaths, filepath.Dir(relativePath))
	}
}

func (i *aidlInterface) checkImports(mctx android.LoadHookContext) {
	for _, anImport := range i.properties.Imports {
		other := lookupInterface(anImport)

		if other == nil {
			mctx.PropertyErrorf("imports", "Import does not exist: "+anImport)
		}

		if i.shouldGenerateCpp() && !other.shouldGenerateCpp() {
			mctx.PropertyErrorf("imports", "Import of gen C++ module must generate C++:"+anImport)
		}
	}
}

func (i *aidlInterface) versionedName(version string) string {
	name := i.ModuleBase.Name()
	if version != "" {
		name = name + "-V" + version
	}
	return name
}

func (i *aidlInterface) srcsForVersion(mctx android.LoadHookContext, version string) (srcs []string, base string) {
	if version == "" {
		return i.properties.Srcs, i.properties.Local_include_dir
	} else {
		var apiDir string
		if i.properties.Api_dir != nil {
			apiDir = *(i.properties.Api_dir)
		} else {
			apiDir = "api"
		}
		base = filepath.Join(apiDir, version)
		full_paths, _ := mctx.GlobWithDeps(filepath.Join(mctx.ModuleDir(), base, "**/*.aidl"), nil)
		for _, path := range full_paths {
			// Here, we need path local to the module
			srcs = append(srcs, strings.TrimPrefix(path, mctx.ModuleDir()+"/"))
		}
		return srcs, base
	}
}

func aidlInterfaceHook(mctx android.LoadHookContext, i *aidlInterface) {
	if !isRelativePath(i.properties.Local_include_dir) {
		mctx.PropertyErrorf("local_include_dir", "must be relative path: "+i.properties.Local_include_dir)
	}

	i.properties.Full_import_path = filepath.Join(mctx.ModuleDir(), i.properties.Local_include_dir)

	i.checkAndUpdateSources(mctx)
	i.checkImports(mctx)

	if mctx.Failed() {
		return
	}

	var libs []string

	if i.shouldGenerateCpp() {
		libs = append(libs, addCppLibrary(mctx, i, ""))
		for _, version := range i.properties.Versions {
			addCppLibrary(mctx, i, version)
		}
	}

	libs = append(libs, addJavaLibrary(mctx, i, ""))
	for _, version := range i.properties.Versions {
		addJavaLibrary(mctx, i, version)
	}

	addApiModule(mctx, i)

	// Reserve this module name for future use
	mctx.CreateModule(android.ModuleFactoryAdaptor(phony.PhonyFactory), &phonyProperties{
		Name:     proptools.StringPtr(i.ModuleBase.Name()),
		Required: libs,
	})
}

func addCppLibrary(mctx android.LoadHookContext, i *aidlInterface, version string) string {
	cppSourceGen := i.versionedName(version) + "-cpp-gen"
	cppModuleGen := i.versionedName(version) + "-cpp"

	srcs, base := i.srcsForVersion(mctx, version)
	if len(srcs) == 0 {
		// This can happen when the version is about to be frozen; the version
		// directory is created but API dump hasn't been copied there.
		// Don't create a library for the yet-to-be-frozen version.
		return ""
	}

	var cppGeneratedSources []string

	for idx, source := range srcs {
		// Use idx to distinguish genrule modules. typename is not appropriate
		// as it is possible to have identical type names in different packages.
		cppSourceGenName := cppSourceGen + "-" + strconv.Itoa(idx)
		mctx.CreateModule(android.ModuleFactoryAdaptor(aidlGenFactory), &nameProperties{
			Name: proptools.StringPtr(cppSourceGenName),
		}, &aidlGenProperties{
			Input:    source,
			AidlRoot: base,
			Imports:  concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
			Lang:     langCpp,
			BaseName: i.ModuleBase.Name(),
		})
		cppGeneratedSources = append(cppGeneratedSources, cppSourceGenName)
	}

	importExportDependencies := concat([]string{
		"libbinder",
		"libutils",
	}, wrap("", i.properties.Imports, "-cpp"))

	mctx.CreateModule(android.ModuleFactoryAdaptor(cc.LibraryFactory), &ccProperties{
		Name:                      proptools.StringPtr(cppModuleGen),
		Owner:                     i.properties.Owner,
		Vendor_available:          i.properties.Vendor_available,
		Defaults:                  []string{"aidl-cpp-module-defaults"},
		Generated_sources:         cppGeneratedSources,
		Generated_headers:         cppGeneratedSources,
		Export_generated_headers:  cppGeneratedSources,
		Shared_libs:               importExportDependencies,
		Export_shared_lib_headers: importExportDependencies,
	}, &i.properties.VndkProperties)

	return cppModuleGen
}

func addJavaLibrary(mctx android.LoadHookContext, i *aidlInterface, version string) string {
	javaSourceGen := i.versionedName(version) + "-java-gen"
	javaModuleGen := i.versionedName(version) + "-java"

	srcs, base := i.srcsForVersion(mctx, version)
	if len(srcs) == 0 {
		// This can happen when the version is about to be frozen; the version
		// directory is created but API dump hasn't been copied there.
		// Don't create a library for the yet-to-be-frozen version.
		return ""
	}

	var javaGeneratedSources []string

	for idx, source := range srcs {
		javaSourceGenName := javaSourceGen + "-" + strconv.Itoa(idx)
		mctx.CreateModule(android.ModuleFactoryAdaptor(aidlGenFactory), &nameProperties{
			Name: proptools.StringPtr(javaSourceGenName),
		}, &aidlGenProperties{
			Input:    source,
			AidlRoot: base,
			Imports:  concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
			Lang:     langJava,
			BaseName: i.ModuleBase.Name(),
		})
		javaGeneratedSources = append(javaGeneratedSources, javaSourceGenName)
	}

	mctx.CreateModule(android.ModuleFactoryAdaptor(java.LibraryFactory), &javaProperties{
		Name:              proptools.StringPtr(javaModuleGen),
		Owner:             i.properties.Owner,
		Installable:       proptools.BoolPtr(true),
		Defaults:          []string{"aidl-java-module-defaults"},
		No_framework_libs: proptools.BoolPtr(true),
		Sdk_version:       proptools.StringPtr("28"),
		Static_libs:       wrap("", i.properties.Imports, "-java"),
		Srcs:              wrap(":", javaGeneratedSources, ""),
	})

	return javaModuleGen
}

func addApiModule(mctx android.LoadHookContext, i *aidlInterface) string {
	apiModule := i.ModuleBase.Name() + aidlApiSuffix
	mctx.CreateModule(android.ModuleFactoryAdaptor(aidlApiFactory), &nameProperties{
		Name: proptools.StringPtr(apiModule),
	}, &aidlApiProperties{
		BaseName: i.ModuleBase.Name(),
		Inputs:   i.properties.Srcs,
		Imports:  concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
		Api_dir:  i.properties.Api_dir,
		AidlRoot: i.properties.Local_include_dir,
		Versions: i.properties.Versions,
	})
	return apiModule
}

func (i *aidlInterface) Name() string {
	return i.ModuleBase.Name() + aidlInterfaceSuffix
}
func (i *aidlInterface) GenerateAndroidBuildActions(ctx android.ModuleContext) {
}
func (i *aidlInterface) DepsMutator(ctx android.BottomUpMutatorContext) {
}

var aidlInterfaceMutex sync.Mutex
var aidlInterfaces []*aidlInterface

func aidlInterfaceFactory() android.Module {
	i := &aidlInterface{}
	i.AddProperties(&i.properties)
	android.InitAndroidModule(i)
	android.AddLoadHook(i, func(ctx android.LoadHookContext) { aidlInterfaceHook(ctx, i) })

	aidlInterfaceMutex.Lock()
	aidlInterfaces = append(aidlInterfaces, i)
	aidlInterfaceMutex.Unlock()

	return i
}

func lookupInterface(name string) *aidlInterface {
	for _, i := range aidlInterfaces {
		if i.ModuleBase.Name() == name {
			return i
		}
	}
	return nil
}
