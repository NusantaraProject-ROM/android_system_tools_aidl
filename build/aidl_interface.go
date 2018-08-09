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
		Description: "AIDL CPP ${in} => ${outDir}",
	}, "imports", "headerDir", "outDir")

	aidlJavaRule = pctx.StaticRule("aidlJavaRule", blueprint.RuleParams{
		Command: `rm -rf "${outDir}" && mkdir -p "${outDir}" && ` +
			`${aidlCmd} --lang=java --structured --ninja -d ${out}.d ` +
			`-o ${outDir} ${imports} ${in}`,
		Depfile:     "${out}.d",
		Deps:        blueprint.DepsGCC,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL Java ${in} => ${outDir}",
	}, "imports", "outDir")
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
	ctx.VisitDirectDeps(func(dep android.Module) {
		importPaths = append(importPaths, dep.(*aidlInterface).properties.Full_import_path)
	})

	imports := strings.Join(wrap("-I", importPaths, ""), " ")

	if g.properties.Lang == langJava {
		ctx.ModuleBuild(pctx, android.ModuleBuildParams{
			Rule:    aidlJavaRule,
			Input:   input,
			Outputs: g.genOutputs,
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
}

func aidlGenFactory() android.Module {
	g := &aidlGenRule{}
	g.AddProperties(&g.properties)
	android.InitAndroidModule(g)
	return g
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
		libs = append(libs, addCppLibrary(mctx, i))
	}

	libs = append(libs, addJavaLibrary(mctx, i))

	// Reserve this module name for future use
	mctx.CreateModule(android.ModuleFactoryAdaptor(phony.PhonyFactory), &phonyProperties{
		Name:     proptools.StringPtr(i.ModuleBase.Name()),
		Required: libs,
	})
}

func addCppLibrary(mctx android.LoadHookContext, i *aidlInterface) string {
	cppSourceGen := i.ModuleBase.Name() + "-cpp-gen"
	cppModuleGen := i.ModuleBase.Name() + "-cpp"

	var cppGeneratedSources []string

	for idx, source := range i.properties.Srcs {
		// Use idx to distinguish genrule modules. typename is not appropriate
		// as it is possible to have identical type names in different packages.
		cppSourceGenName := cppSourceGen + "-" + strconv.Itoa(idx)
		mctx.CreateModule(android.ModuleFactoryAdaptor(aidlGenFactory), &nameProperties{
			Name: proptools.StringPtr(cppSourceGenName),
		}, &aidlGenProperties{
			Input:    source,
			AidlRoot: i.properties.Local_include_dir,
			Imports:  concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
			Lang:     langCpp,
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

func addJavaLibrary(mctx android.LoadHookContext, i *aidlInterface) string {
	javaSourceGen := i.ModuleBase.Name() + "-java-gen"
	javaModuleGen := i.ModuleBase.Name() + "-java"

	var javaGeneratedSources []string

	for idx, source := range i.properties.Srcs {
		javaSourceGenName := javaSourceGen + "-" + strconv.Itoa(idx)
		mctx.CreateModule(android.ModuleFactoryAdaptor(aidlGenFactory), &nameProperties{
			Name: proptools.StringPtr(javaSourceGenName),
		}, &aidlGenProperties{
			Input:    source,
			AidlRoot: i.properties.Local_include_dir,
			Imports:  concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
			Lang:     langJava,
		})
		javaGeneratedSources = append(javaGeneratedSources, javaSourceGenName)
	}

	mctx.CreateModule(android.ModuleFactoryAdaptor(java.LibraryFactory), &javaProperties{
		Name:              proptools.StringPtr(javaModuleGen),
		Owner:             i.properties.Owner,
		Installable:       proptools.BoolPtr(true),
		Defaults:          []string{"aidl-java-module-defaults"},
		No_framework_libs: proptools.BoolPtr(true),
		Sdk_version:       proptools.StringPtr("current"),
		Static_libs:       wrap("", i.properties.Imports, "-java"),
		Srcs:              wrap(":", javaGeneratedSources, ""),
	})

	return javaModuleGen
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
