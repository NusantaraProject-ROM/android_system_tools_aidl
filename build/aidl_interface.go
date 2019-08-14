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
	"android/soong/android"
	"android/soong/cc"
	"android/soong/genrule"
	"android/soong/java"
	"android/soong/phony"

	"fmt"
	"io"
	"path/filepath"
	"runtime"
	"strconv"
	"strings"
	"sync"

	"github.com/google/blueprint"
	"github.com/google/blueprint/pathtools"
	"github.com/google/blueprint/proptools"
)

var (
	aidlInterfaceSuffix = "_interface"
	aidlApiSuffix       = "-api"
	langCpp             = "cpp"
	langJava            = "java"
	langNdk             = "ndk"
	langNdkPlatform     = "ndk_platform"
	futureVersion       = "10000"

	pctx = android.NewPackageContext("android/aidl")

	aidlDirPrepareRule = pctx.StaticRule("aidlDirPrepareRule", blueprint.RuleParams{
		Command: `rm -rf "${outDir}" && mkdir -p "${outDir}" && ` +
			`touch ${out}`,
		Description: "create ${out}",
	}, "outDir")

	aidlCppRule = pctx.StaticRule("aidlCppRule", blueprint.RuleParams{
		Command: `mkdir -p "${headerDir}" && ` +
			`${aidlCmd} --lang=${lang} ${optionalFlags} --structured --ninja -d ${out}.d ` +
			`-h ${headerDir} -o ${outDir} ${imports} ${in}`,
		Depfile:     "${out}.d",
		Deps:        blueprint.DepsGCC,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL ${lang} ${in}",
	}, "imports", "lang", "headerDir", "outDir", "optionalFlags")

	aidlJavaRule = pctx.StaticRule("aidlJavaRule", blueprint.RuleParams{
		Command: `${aidlCmd} --lang=java ${optionalFlags} --structured --ninja -d ${out}.d ` +
			`-o ${outDir} ${imports} ${in}`,
		Depfile:     "${out}.d",
		Deps:        blueprint.DepsGCC,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL Java ${in}",
	}, "imports", "outDir", "optionalFlags")

	aidlDumpApiRule = pctx.StaticRule("aidlDumpApiRule", blueprint.RuleParams{
		Command: `rm -rf "${outDir}" && mkdir -p "${outDir}" && ` +
			`${aidlCmd} --dumpapi --structured ${imports} --out ${outDir} ${in} && ` +
			`(cd ${outDir} && find ./ -name "*.aidl" -exec ${sha1sum} {} ';' && echo ${latestVersion}) | ${sha1sum} > ${hashFile} `,
		CommandDeps: []string{"${aidlCmd}"},
	}, "imports", "outDir", "hashFile", "latestVersion", "sha1sum")

	aidlDumpMappingsRule = pctx.StaticRule("aidlDumpMappingsRule", blueprint.RuleParams{
		Command: `rm -rf "${outDir}" && mkdir -p "${outDir}" && ` +
			`${aidlCmd} --apimapping ${outDir}/intermediate.txt ${in} ${imports} && ` +
			`${aidlToJniCmd} ${outDir}/intermediate.txt ${out}`,
		CommandDeps: []string{"${aidlCmd}"},
	}, "imports", "outDir")

	aidlFreezeApiRule = pctx.AndroidStaticRule("aidlFreezeApiRule",
		blueprint.RuleParams{
			Command: `mkdir -p ${to} && rm -rf ${to}/* && ` +
				`${bpmodifyCmd} -w -m ${name} -parameter versions -a ${version} ${bp} && ` +
				`cp -rf ${apiDir}/* ${to} && ` +
				`find ${to} -type f -exec bash -c ` +
				`"cat ${apiPreamble} {} > {}.temp; mv {}.temp {}" \; && ` +
				`touch ${out}`,
			CommandDeps: []string{"${bpmodifyCmd}"},
		}, "to", "name", "version", "bp", "apiDir", "apiPreamble")

	aidlCheckApiRule = pctx.StaticRule("aidlCheckApiRule", blueprint.RuleParams{
		Command: `(${aidlCmd} --checkapi ${old} ${new} && touch ${out}) || ` +
			`(cat ${messageFile} && exit 1)`,
		CommandDeps: []string{"${aidlCmd}"},
		Description: "AIDL CHECK API: ${new} against ${old}",
	}, "old", "new", "messageFile")

	aidlDiffApiRule = pctx.StaticRule("aidlDiffApiRule", blueprint.RuleParams{
		Command: `(diff -N --line-format="" ${oldHashFile} ${newHashFile} && diff -r -B -I '//.*' ${old} ${new} && touch ${out}) || ` +
			`(cat ${messageFile} && exit 1)`,
		Description: "Check equality of ${new} and ${old}",
	}, "old", "new", "messageFile", "oldHashFile", "newHashFile")
)

func init() {
	pctx.HostBinToolVariable("aidlCmd", "aidl")
	pctx.HostBinToolVariable("bpmodifyCmd", "bpmodify")
	pctx.SourcePathVariable("aidlToJniCmd", "system/tools/aidl/build/aidl_to_jni.py")
	android.RegisterModuleType("aidl_interface", aidlInterfaceFactory)
	android.RegisterModuleType("aidl_mapping", aidlMappingFactory)
	android.RegisterMakeVarsProvider(pctx, allAidlInterfacesMakeVars)
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

func checkAndUpdateSources(ctx android.ModuleContext, rawSrcs []string, inDir string) android.Paths {
	srcs := android.PathsForModuleSrc(ctx, rawSrcs)
	srcs = android.PathsWithModuleSrcSubDir(ctx, srcs, inDir)

	if len(srcs) == 0 {
		ctx.PropertyErrorf("srcs", "No sources provided.")
	}

	for _, source := range srcs {
		if source.Ext() != ".aidl" {
			ctx.PropertyErrorf("srcs", "Source must be a .aidl file: "+source.String())
			continue
		}
	}

	return srcs
}

func isRelativePath(path string) bool {
	if path == "" {
		return true
	}
	return filepath.Clean(path) == path && path != ".." &&
		!strings.HasPrefix(path, "../") && !strings.HasPrefix(path, "/")
}

type aidlGenProperties struct {
	Srcs      []string `android:"path"`
	AidlRoot  string   // base directory for the input aidl file
	Imports   []string
	Stability *string
	Lang      string // target language [java|cpp|ndk]
	BaseName  string
	GenLog    bool
	Version   string
}

type aidlGenRule struct {
	android.ModuleBase

	properties aidlGenProperties

	implicitInputs android.Paths
	importFlags    string

	genOutDir     android.ModuleGenPath
	genHeaderDir  android.ModuleGenPath
	genHeaderDeps android.Paths
	genOutputs    android.WritablePaths
}

var _ android.SourceFileProducer = (*aidlGenRule)(nil)
var _ genrule.SourceFileGenerator = (*aidlGenRule)(nil)

func (g *aidlGenRule) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	srcs := checkAndUpdateSources(ctx, g.properties.Srcs, g.properties.AidlRoot)

	if ctx.Failed() {
		return
	}

	genDirTimestamp := android.PathForModuleGen(ctx, "timestamp")
	g.implicitInputs = append(g.implicitInputs, genDirTimestamp)

	var importPaths []string
	ctx.VisitDirectDeps(func(dep android.Module) {
		if importedAidl, ok := dep.(*aidlInterface); ok {
			importPaths = append(importPaths, importedAidl.properties.Full_import_paths...)
		} else if api, ok := dep.(*aidlApi); ok {
			// When compiling an AIDL interface, also make sure that each
			// version of the interface is compatible with its previous version
			for _, path := range api.checkApiTimestamps {
				g.implicitInputs = append(g.implicitInputs, path)
			}
		}
	})
	g.importFlags = strings.Join(wrap("-I", importPaths, ""), " ")

	g.genOutDir = android.PathForModuleGen(ctx)
	g.genHeaderDir = android.PathForModuleGen(ctx, "include")
	for _, src := range srcs {
		outFile, headers := g.generateBuildActionsForSingleAidl(ctx, src)
		g.genOutputs = append(g.genOutputs, outFile)
		g.genHeaderDeps = append(g.genHeaderDeps, headers...)
	}

	// This is to clean genOutDir before generating any file
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:      aidlDirPrepareRule,
		Implicits: srcs,
		Output:    genDirTimestamp,
		Args: map[string]string{
			"outDir": g.genOutDir.String(),
		},
	})
}

func (g *aidlGenRule) generateBuildActionsForSingleAidl(ctx android.ModuleContext, src android.Path) (android.WritablePath, android.Paths) {
	var outFile android.WritablePath
	if g.properties.Lang == langJava {
		outFile = android.PathForModuleGen(ctx, pathtools.ReplaceExtension(src.Rel(), "java"))
	} else {
		outFile = android.PathForModuleGen(ctx, pathtools.ReplaceExtension(src.Rel(), "cpp"))
	}

	var optionalFlags []string
	if g.properties.Version != "" {
		optionalFlags = append(optionalFlags, "--version "+g.properties.Version)
	}

	var headers android.WritablePaths
	if g.properties.Lang == langJava {
		ctx.ModuleBuild(pctx, android.ModuleBuildParams{
			Rule:      aidlJavaRule,
			Input:     src,
			Implicits: g.implicitInputs,
			Output:    outFile,
			Args: map[string]string{
				"imports":       g.importFlags,
				"outDir":        g.genOutDir.String(),
				"optionalFlags": strings.Join(optionalFlags, " "),
			},
		})
	} else {
		typeName := strings.TrimSuffix(filepath.Base(src.Rel()), ".aidl")
		packagePath := filepath.Dir(src.Rel())
		baseName := typeName
		// TODO(b/111362593): aidl_to_cpp_common.cpp uses heuristics to figure out if
		//   an interface name has a leading I. Those same heuristics have been
		//   moved here.
		if len(baseName) >= 2 && baseName[0] == 'I' &&
			strings.ToUpper(baseName)[1] == baseName[1] {
			baseName = strings.TrimPrefix(typeName, "I")
		}

		prefix := ""
		if g.properties.Lang == langNdk || g.properties.Lang == langNdkPlatform {
			prefix = "aidl"
		}

		headers = append(headers, g.genHeaderDir.Join(ctx, prefix, packagePath,
			typeName+".h"))
		headers = append(headers, g.genHeaderDir.Join(ctx, prefix, packagePath,
			"Bp"+baseName+".h"))
		headers = append(headers, g.genHeaderDir.Join(ctx, prefix, packagePath,
			"Bn"+baseName+".h"))

		if g.properties.GenLog {
			optionalFlags = append(optionalFlags, "--log")
		}

		if g.properties.Stability != nil {
			optionalFlags = append(optionalFlags, "--stability", *g.properties.Stability)
		}

		aidlLang := g.properties.Lang
		if aidlLang == langNdkPlatform {
			aidlLang = "ndk"
		}

		ctx.ModuleBuild(pctx, android.ModuleBuildParams{
			Rule:            aidlCppRule,
			Input:           src,
			Implicits:       g.implicitInputs,
			Output:          outFile,
			ImplicitOutputs: headers,
			Args: map[string]string{
				"imports":       g.importFlags,
				"lang":          aidlLang,
				"headerDir":     g.genHeaderDir.String(),
				"outDir":        g.genOutDir.String(),
				"optionalFlags": strings.Join(optionalFlags, " "),
			},
		})
	}

	return outFile, headers.Paths()
}

func (g *aidlGenRule) GeneratedSourceFiles() android.Paths {
	return g.genOutputs.Paths()
}

func (g *aidlGenRule) Srcs() android.Paths {
	return g.genOutputs.Paths()
}

func (g *aidlGenRule) GeneratedDeps() android.Paths {
	return g.genHeaderDeps
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
	Srcs     []string `android:"path"`
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

// Version of the interface at ToT if it is frozen
func (m *aidlApi) validateCurrentVersion(ctx android.ModuleContext) string {
	if len(m.properties.Versions) == 0 {
		return "1"
	} else {
		latestVersion := m.properties.Versions[len(m.properties.Versions)-1]

		i, err := strconv.ParseInt(latestVersion, 10, 64)
		if err != nil {
			ctx.PropertyErrorf("versions", "must be integers")
			return ""
		}

		return strconv.FormatInt(i+1, 10)
	}
}

func (m *aidlApi) createApiDumpFromSource(ctx android.ModuleContext) (apiDir android.WritablePath, apiFiles android.WritablePaths, hashFile android.WritablePath) {
	srcs := checkAndUpdateSources(ctx, m.properties.Srcs, m.properties.AidlRoot)

	if ctx.Failed() {
		return
	}

	var importPaths []string
	ctx.VisitDirectDeps(func(dep android.Module) {
		if importedAidl, ok := dep.(*aidlInterface); ok {
			importPaths = append(importPaths, importedAidl.properties.Full_import_paths...)
		}
	})

	apiDir = android.PathForModuleOut(ctx, "dump")
	for _, src := range srcs {
		apiFiles = append(apiFiles, android.PathForModuleOut(ctx, "dump", src.Rel()))
	}
	hashFile = android.PathForModuleOut(ctx, "dump", ".hash")
	latestVersion := "latest-version"
	if len(m.properties.Versions) >= 1 {
		latestVersion = m.properties.Versions[len(m.properties.Versions)-1]
	}
	imports := strings.Join(wrap("-I", importPaths, ""), " ")
	sha1sum := "sha1sum"
	if runtime.GOOS == "darwin" {
		sha1sum = "shasum" // on Mac, we have shasum
	}
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:    aidlDumpApiRule,
		Outputs: append(apiFiles, hashFile),
		Inputs:  srcs,
		Args: map[string]string{
			"imports":       imports,
			"outDir":        apiDir.String(),
			"hashFile":      hashFile.String(),
			"latestVersion": latestVersion,
			"sha1sum":       sha1sum,
		},
	})
	return apiDir, apiFiles, hashFile
}

func (m *aidlApi) freezeApiDumpAsVersion(ctx android.ModuleContext, apiDumpDir android.Path, apiFiles android.Paths, version string) android.WritablePath {
	timestampFile := android.PathForModuleOut(ctx, "freezeapi_"+version+".timestamp")

	modulePath := android.PathForModuleSrc(ctx).String()

	var implicits android.Paths
	implicits = append(implicits, apiFiles...)

	apiPreamble := android.PathForSource(ctx, "system/tools/aidl/build/api_preamble.txt")
	implicits = append(implicits, apiPreamble)

	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:        aidlFreezeApiRule,
		Description: "Freezing AIDL API of " + m.properties.BaseName + " as version " + version,
		Implicits:   implicits,
		Output:      timestampFile,
		Args: map[string]string{
			"to":          filepath.Join(modulePath, m.apiDir(), version),
			"apiDir":      apiDumpDir.String(),
			"name":        m.properties.BaseName,
			"version":     version,
			"bp":          android.PathForModuleSrc(ctx, "Android.bp").String(),
			"apiPreamble": apiPreamble.String(),
		},
	})
	return timestampFile
}

func (m *aidlApi) checkCompatibility(ctx android.ModuleContext, oldApiDir android.Path, oldApiFiles android.Paths, newApiDir android.Path, newApiFiles android.Paths) android.WritablePath {
	newVersion := newApiDir.Base()
	timestampFile := android.PathForModuleOut(ctx, "checkapi_"+newVersion+".timestamp")
	messageFile := android.PathForSource(ctx, "system/tools/aidl/build/message_check_compatibility.txt")
	var implicits android.Paths
	implicits = append(implicits, oldApiFiles...)
	implicits = append(implicits, newApiFiles...)
	implicits = append(implicits, messageFile)
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:      aidlCheckApiRule,
		Implicits: implicits,
		Output:    timestampFile,
		Args: map[string]string{
			"old":         oldApiDir.String(),
			"new":         newApiDir.String(),
			"messageFile": messageFile.String(),
		},
	})
	return timestampFile
}

func (m *aidlApi) checkEquality(ctx android.ModuleContext, oldApiDir android.Path, oldApiFiles android.Paths, oldHashFile android.OptionalPath,
	newApiDir android.Path, newApiFiles android.Paths, newHashFile android.Path) android.WritablePath {
	newVersion := newApiDir.Base()
	timestampFile := android.PathForModuleOut(ctx, "checkapi_"+newVersion+".timestamp")
	messageFile := android.PathForSource(ctx, "system/tools/aidl/build/message_check_equality.txt")
	var implicits android.Paths
	implicits = append(implicits, oldApiFiles...)
	implicits = append(implicits, newApiFiles...)
	implicits = append(implicits, messageFile)
	if oldHashFile.Valid() {
		implicits = append(implicits, oldHashFile.Path())
	}
	implicits = append(implicits, newHashFile)
	ctx.ModuleBuild(pctx, android.ModuleBuildParams{
		Rule:      aidlDiffApiRule,
		Implicits: implicits,
		Output:    timestampFile,
		Args: map[string]string{
			"old":         oldApiDir.String(),
			"new":         newApiDir.String(),
			"messageFile": messageFile.String(),
			"oldHashFile": oldHashFile.String(),
			"newHashFile": newHashFile.String(),
		},
	})
	return timestampFile
}

func (m *aidlApi) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	currentVersion := m.validateCurrentVersion(ctx)
	currentDumpDir, currentApiFiles, currentHashFile := m.createApiDumpFromSource(ctx)

	if ctx.Failed() {
		return
	}

	m.freezeApiTimestamp = m.freezeApiDumpAsVersion(ctx, currentDumpDir, currentApiFiles.Paths(), currentVersion)

	apiDirs := make(map[string]android.Path)
	apiFiles := make(map[string]android.Paths)
	for _, ver := range m.properties.Versions {
		apiDir := android.PathForModuleSrc(ctx, m.apiDir(), ver)
		apiDirs[ver] = apiDir
		apiFiles[ver] = ctx.Glob(filepath.Join(apiDir.String(), "**/*.aidl"), nil)
	}
	apiDirs[currentVersion] = currentDumpDir
	apiFiles[currentVersion] = currentApiFiles.Paths()

	// Check that version X is backward compatible with version X-1
	for i, newVersion := range m.properties.Versions {
		if i != 0 {
			oldVersion := m.properties.Versions[i-1]
			checkApiTimestamp := m.checkCompatibility(ctx, apiDirs[oldVersion], apiFiles[oldVersion], apiDirs[newVersion], apiFiles[newVersion])
			m.checkApiTimestamps = append(m.checkApiTimestamps, checkApiTimestamp)
		}
	}

	// ... and that the currentVersion (ToT) is backwards compatible with or
	// equal to the latest frozen version
	if len(m.properties.Versions) >= 1 {
		latestVersion := m.properties.Versions[len(m.properties.Versions)-1]
		var checkApiTimestamp android.WritablePath
		if ctx.Config().DefaultAppTargetSdkInt() != android.FutureApiLevel {
			// If API is frozen, don't allow any change to the API
			latestHashFile := android.OptionalPathForModuleSrc(ctx, proptools.StringPtr(filepath.Join(m.apiDir(), latestVersion, ".hash")))
			checkApiTimestamp = m.checkEquality(ctx, apiDirs[latestVersion], apiFiles[latestVersion], latestHashFile,
				apiDirs[currentVersion], apiFiles[currentVersion], currentHashFile)
		} else {
			// If not, allow backwards compatible changes to the API
			checkApiTimestamp = m.checkCompatibility(ctx, apiDirs[latestVersion], apiFiles[latestVersion], apiDirs[currentVersion], apiFiles[currentVersion])
		}
		m.checkApiTimestamps = append(m.checkApiTimestamps, checkApiTimestamp)
	}
}

func (m *aidlApi) AndroidMk() android.AndroidMkData {
	return android.AndroidMkData{
		Custom: func(w io.Writer, name, prefix, moduleDir string, data android.AndroidMkData) {
			android.WriteAndroidMkData(w, data)
			targetName := m.properties.BaseName + "-freeze-api"
			fmt.Fprintln(w, ".PHONY:", targetName)
			fmt.Fprintln(w, targetName+":", m.freezeApiTimestamp.String())
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
	// Top level directories for includes.
	// TODO(b/128940869): remove it if aidl_interface can depend on framework.aidl
	Include_dirs []string
	// Relative path for includes. By default assumes AIDL path is relative to current directory.
	// TODO(b/111117220): automatically compute by letting AIDL parse multiple files simultaneously
	Local_include_dir string

	// List of .aidl files which compose this interface.
	Srcs []string `android:"path"`

	// List of aidl_interface modules that this uses. If one of your AIDL interfaces uses an
	// interface or parcelable from another aidl_interface, you should put its name here.
	Imports []string

	// Used by gen dependency to fill out aidl include path
	Full_import_paths []string `blueprint:"mutated"`

	// Directory where API dumps are. Default is "api".
	Api_dir *string

	// Stability promise. Currently only supports "vintf".
	// If this is unset, this corresponds to an interface with stability within
	// this compilation context (so an interface loaded here can only be used
	// with things compiled together, e.g. on the system.img).
	// If this is set to "vintf", this corresponds to a stability promise: the
	// interface must be kept stable as long as it is used.
	Stability *string

	// Previous API versions that are now frozen. The version that is last in
	// the list is considered as the most recent version.
	Versions []string

	Backend struct {
		Java struct {
			// Whether to generate Java code using Java binder APIs
			// Default: true
			Enabled *bool
			// Set to the version of the sdk to compile against
			// Default: system_current
			Sdk_version *string
		}
		Cpp struct {
			// Whether to generate C++ code using C++ binder APIs
			// Default: true
			Enabled *bool
			// Whether to generate additional code for gathering information
			// about the transactions
			// Default: false
			Gen_log *bool
		}
		Ndk struct {
			// Whether to generate C++ code using NDK binder APIs
			// Default: true
			Enabled *bool
			// Whether to generate additional code for gathering information
			// about the transactions
			// Default: false
			Gen_log *bool
		}
	}
}

type aidlInterface struct {
	android.ModuleBase

	properties aidlInterfaceProperties
}

func (i *aidlInterface) shouldGenerateJavaBackend() bool {
	// explicitly true if not specified to give early warning to devs
	return i.properties.Backend.Java.Enabled == nil || *i.properties.Backend.Java.Enabled
}

func (i *aidlInterface) shouldGenerateCppBackend() bool {
	// explicitly true if not specified to give early warning to devs
	return i.properties.Backend.Cpp.Enabled == nil || *i.properties.Backend.Cpp.Enabled
}

func (i *aidlInterface) shouldGenerateNdkBackend() bool {
	// explicitly true if not specified to give early warning to devs
	return i.properties.Backend.Ndk.Enabled == nil || *i.properties.Backend.Ndk.Enabled
}

func (i *aidlInterface) checkImports(mctx android.LoadHookContext) {
	for _, anImport := range i.properties.Imports {
		other := lookupInterface(anImport)

		if other == nil {
			mctx.PropertyErrorf("imports", "Import does not exist: "+anImport)
		}

		if i.shouldGenerateJavaBackend() && !other.shouldGenerateJavaBackend() {
			mctx.PropertyErrorf("backend.java.enabled",
				"Java backend not enabled in the imported AIDL interface %q", anImport)
		}

		if i.shouldGenerateCppBackend() && !other.shouldGenerateCppBackend() {
			mctx.PropertyErrorf("backend.cpp.enabled",
				"C++ backend not enabled in the imported AIDL interface %q", anImport)
		}

		if i.shouldGenerateNdkBackend() && !other.shouldGenerateNdkBackend() {
			mctx.PropertyErrorf("backend.ndk.enabled",
				"NDK backend not enabled in the imported AIDL interface %q", anImport)
		}
	}
}

func (i *aidlInterface) checkStability(mctx android.LoadHookContext) {
	if i.properties.Stability == nil {
		return
	}

	// TODO(b/136027762): should we allow more types of stability (e.g. for APEX) or
	// should we switch this flag to be something like "vintf { enabled: true }"
	if *i.properties.Stability != "vintf" {
		mctx.PropertyErrorf("stability", "must be empty or \"vintf\"")
	}

	// TODO(b/136027762): need some global way to understand AOSP interfaces. Also,
	// need the implementation for vendor extensions to be merged. For now, restrict
	// where this can be defined
	if !filepath.HasPrefix(mctx.ModuleDir(), "hardware/interfaces/") {
		mctx.PropertyErrorf("stability", "can only be set in hardware/interfaces")
	}
}

func (i *aidlInterface) versionedName(version string) string {
	name := i.ModuleBase.Name()
	if version != futureVersion && version != "" {
		name = name + "-V" + version
	}
	return name
}

func (i *aidlInterface) srcsForVersion(mctx android.LoadHookContext, version string) (srcs []string, base string) {
	if version == futureVersion || version == "" {
		return i.properties.Srcs, i.properties.Local_include_dir
	} else {
		var apiDir string
		if i.properties.Api_dir != nil {
			apiDir = *(i.properties.Api_dir)
		} else {
			apiDir = "api"
		}
		base = filepath.Join(apiDir, version)
		full_paths, err := mctx.GlobWithDeps(filepath.Join(mctx.ModuleDir(), base, "**/*.aidl"), nil)
		if err != nil {
			panic(err)
		}
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
	var importPaths []string
	importPaths = append(importPaths, filepath.Join(mctx.ModuleDir(), i.properties.Local_include_dir))
	importPaths = append(importPaths, i.properties.Include_dirs...)

	i.properties.Full_import_paths = importPaths

	i.checkImports(mctx)
	i.checkStability(mctx)

	if mctx.Failed() {
		return
	}

	var libs []string

	currentVersion := ""
	if len(i.properties.Versions) > 0 {
		currentVersion = futureVersion
	}

	if i.shouldGenerateCppBackend() {
		libs = append(libs, addCppLibrary(mctx, i, currentVersion, langCpp))
		for _, version := range i.properties.Versions {
			addCppLibrary(mctx, i, version, langCpp)
		}
	}

	if i.shouldGenerateNdkBackend() {
		// TODO(b/119771576): inherit properties and export 'is vendor' computation from cc.go
		if !proptools.Bool(i.properties.Vendor_available) {
			libs = append(libs, addCppLibrary(mctx, i, currentVersion, langNdk))
			for _, version := range i.properties.Versions {
				addCppLibrary(mctx, i, version, langNdk)
			}
		}
		// TODO(b/121157555): combine with '-ndk' variant
		libs = append(libs, addCppLibrary(mctx, i, currentVersion, langNdkPlatform))
		for _, version := range i.properties.Versions {
			addCppLibrary(mctx, i, version, langNdkPlatform)
		}
	}

	if i.shouldGenerateJavaBackend() {
		libs = append(libs, addJavaLibrary(mctx, i, currentVersion))
		for _, version := range i.properties.Versions {
			addJavaLibrary(mctx, i, version)
		}
	}

	addApiModule(mctx, i)

	// Reserve this module name for future use
	mctx.CreateModule(android.ModuleFactoryAdaptor(phony.PhonyFactory), &phonyProperties{
		Name:     proptools.StringPtr(i.ModuleBase.Name()),
		Required: libs,
	})
}

func addCppLibrary(mctx android.LoadHookContext, i *aidlInterface, version string, lang string) string {
	cppSourceGen := i.versionedName(version) + "-" + lang + "-source"
	cppModuleGen := i.versionedName(version) + "-" + lang

	srcs, base := i.srcsForVersion(mctx, version)
	if len(srcs) == 0 {
		// This can happen when the version is about to be frozen; the version
		// directory is created but API dump hasn't been copied there.
		// Don't create a library for the yet-to-be-frozen version.
		return ""
	}

	genLog := false
	if lang == langCpp {
		genLog = proptools.Bool(i.properties.Backend.Cpp.Gen_log)
	} else if lang == langNdk || lang == langNdkPlatform {
		genLog = proptools.Bool(i.properties.Backend.Ndk.Gen_log)
	}

	mctx.CreateModule(android.ModuleFactoryAdaptor(aidlGenFactory), &nameProperties{
		Name: proptools.StringPtr(cppSourceGen),
	}, &aidlGenProperties{
		Srcs:      srcs,
		AidlRoot:  base,
		Imports:   concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
		Stability: i.properties.Stability,
		Lang:      lang,
		BaseName:  i.ModuleBase.Name(),
		GenLog:    genLog,
		Version:   version,
	})

	importExportDependencies := wrap("", i.properties.Imports, "-"+lang)
	var libJSONCppDependency []string
	var staticLibDependency []string
	var sdkVersion *string
	var stl *string
	var cpp_std *string
	if lang == langCpp {
		importExportDependencies = append(importExportDependencies, "libbinder", "libutils")
		if genLog {
			libJSONCppDependency = []string{"libjsoncpp"}
		}
		sdkVersion = nil
		stl = nil
		cpp_std = nil
	} else if lang == langNdk {
		importExportDependencies = append(importExportDependencies, "libbinder_ndk")
		if genLog {
			staticLibDependency = []string{"libjsoncpp_ndk"}
		}
		sdkVersion = proptools.StringPtr("current")
		stl = proptools.StringPtr("c++_shared")
	} else if lang == langNdkPlatform {
		importExportDependencies = append(importExportDependencies, "libbinder_ndk")
		if genLog {
			libJSONCppDependency = []string{"libjsoncpp"}
		}
	} else {
		panic("Unrecognized language: " + lang)
	}

	mctx.CreateModule(android.ModuleFactoryAdaptor(cc.LibraryFactory), &ccProperties{
		Name:                      proptools.StringPtr(cppModuleGen),
		Vendor_available:          i.properties.Vendor_available,
		Defaults:                  []string{"aidl-cpp-module-defaults"},
		Generated_sources:         []string{cppSourceGen},
		Generated_headers:         []string{cppSourceGen},
		Export_generated_headers:  []string{cppSourceGen},
		Static:                    staticLib{Whole_static_libs: libJSONCppDependency},
		Shared:                    sharedLib{Shared_libs: libJSONCppDependency, Export_shared_lib_headers: libJSONCppDependency},
		Static_libs:               staticLibDependency,
		Shared_libs:               importExportDependencies,
		Export_shared_lib_headers: importExportDependencies,
		Sdk_version:               sdkVersion,
		Stl:                       stl,
		Cpp_std:                   cpp_std,
		Cflags:                    []string{"-Wextra", "-Wall", "-Werror"},
	}, &i.properties.VndkProperties)

	return cppModuleGen
}

func addJavaLibrary(mctx android.LoadHookContext, i *aidlInterface, version string) string {
	javaSourceGen := i.versionedName(version) + "-java-source"
	javaModuleGen := i.versionedName(version) + "-java"

	srcs, base := i.srcsForVersion(mctx, version)
	if len(srcs) == 0 {
		// This can happen when the version is about to be frozen; the version
		// directory is created but API dump hasn't been copied there.
		// Don't create a library for the yet-to-be-frozen version.
		return ""
	}

	sdkVersion := proptools.StringDefault(i.properties.Backend.Java.Sdk_version, "system_current")

	mctx.CreateModule(android.ModuleFactoryAdaptor(aidlGenFactory), &nameProperties{
		Name: proptools.StringPtr(javaSourceGen),
	}, &aidlGenProperties{
		Srcs:      srcs,
		AidlRoot:  base,
		Imports:   concat(i.properties.Imports, []string{i.ModuleBase.Name()}),
		Stability: i.properties.Stability,
		Lang:      langJava,
		BaseName:  i.ModuleBase.Name(),
		Version:   version,
	})

	mctx.CreateModule(android.ModuleFactoryAdaptor(java.LibraryFactory), &javaProperties{
		Name:        proptools.StringPtr(javaModuleGen),
		Installable: proptools.BoolPtr(true),
		Defaults:    []string{"aidl-java-module-defaults"},
		Sdk_version: proptools.StringPtr(sdkVersion),
		Static_libs: wrap("", i.properties.Imports, "-java"),
		Srcs:        []string{":" + javaSourceGen},
	})

	return javaModuleGen
}

func addApiModule(mctx android.LoadHookContext, i *aidlInterface) string {
	apiModule := i.ModuleBase.Name() + aidlApiSuffix
	mctx.CreateModule(android.ModuleFactoryAdaptor(aidlApiFactory), &nameProperties{
		Name: proptools.StringPtr(apiModule),
	}, &aidlApiProperties{
		BaseName: i.ModuleBase.Name(),
		Srcs:     i.properties.Srcs,
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

type aidlMappingProperties struct {
	// Source file of this prebuilt.
	Srcs   []string `android:"arch_variant"`
	Output string
}

type aidlMapping struct {
	android.ModuleBase
	properties     aidlMappingProperties
	outputFilePath android.WritablePath
}

func (s *aidlMapping) DepsMutator(ctx android.BottomUpMutatorContext) {
	android.ExtractSourcesDeps(ctx, s.properties.Srcs)
}

func (s *aidlMapping) GenerateAndroidBuildActions(ctx android.ModuleContext) {
	var srcs android.Paths
	var allImportDirs []string
	seenImportDirs := make(map[string]bool)

	addImportDirs := func(dirs ...string) {
		for _, dir := range dirs {
			if !seenImportDirs[dir] {
				allImportDirs = append(allImportDirs, dir)
				seenImportDirs[dir] = true
			}
		}
	}

	ctx.VisitDirectDeps(func(module android.Module) {
		for _, property := range module.GetProperties() {
			if jproperty, ok := property.(*java.CompilerProperties); ok {
				for _, src := range jproperty.Srcs {
					if strings.HasSuffix(src, ".aidl") {
						full_path := android.PathForModuleSrc(ctx, src)
						srcs = append(srcs, full_path)
						addImportDirs(filepath.Dir(full_path.String()))
					} else if pathtools.IsGlob(src) {
						globbedSrcFiles, err := ctx.GlobWithDeps(src, nil)
						if err == nil {
							for _, globbedSrc := range globbedSrcFiles {
								full_path := android.PathForModuleSrc(ctx, globbedSrc)
								addImportDirs(full_path.String())
							}
						}
					}
				}
			} else if jproperty, ok := property.(*java.CompilerDeviceProperties); ok {
				addImportDirs(jproperty.Aidl.Include_dirs...)
				for _, include_dir := range jproperty.Aidl.Export_include_dirs {
					var full_path = filepath.Join(ctx.ModuleDir(), include_dir)
					addImportDirs(full_path)
				}
				for _, include_dir := range jproperty.Aidl.Local_include_dirs {
					var full_path = filepath.Join(ctx.ModuleSubDir(), include_dir)
					addImportDirs(full_path)
				}
			}
		}
	})

	imports := strings.Join(wrap("-I", allImportDirs, ""), " ")
	s.outputFilePath = android.PathForModuleOut(ctx, s.properties.Output)
	outDir := android.PathForModuleGen(ctx)
	ctx.Build(pctx, android.BuildParams{
		Rule:   aidlDumpMappingsRule,
		Inputs: srcs,
		Output: s.outputFilePath,
		Args: map[string]string{
			"imports": imports,
			"outDir":  outDir.String(),
		},
	})
}

func InitAidlMappingModule(s *aidlMapping) {
	s.AddProperties(&s.properties)
}

func aidlMappingFactory() android.Module {
	module := &aidlMapping{}
	InitAidlMappingModule(module)
	android.InitAndroidModule(module)
	return module
}

func (m *aidlMapping) AndroidMk() android.AndroidMkData {
	return android.AndroidMkData{
		Custom: func(w io.Writer, name, prefix, moduleDir string, data android.AndroidMkData) {
			android.WriteAndroidMkData(w, data)
			targetName := m.Name()
			fmt.Fprintln(w, ".PHONY:", targetName)
			fmt.Fprintln(w, targetName+":", m.outputFilePath.String())
		},
	}
}

func allAidlInterfacesMakeVars(ctx android.MakeVarsContext) {
	names := []string{}
	ctx.VisitAllModules(func(module android.Module) {
		if ai, ok := module.(*aidlInterface); ok {
			names = append(names, ai.Name())
		}
	})
	ctx.Strict("ALL_AIDL_INTERFACES", strings.Join(names, " "))
}
