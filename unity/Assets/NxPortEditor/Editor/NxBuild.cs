// NxBuild.cs -- сборка проекта ukport (код ULTRAKILL + слой NxPort) из батч-режима.
//
//   Unity.exe -batchmode -quit -projectPath ukport -executeMethod NxBuild.WindowsGles
//   Unity.exe -batchmode -quit -projectPath ukport -executeMethod NxBuild.Android
//
// WindowsGles -- стенд на ПК: Windows-плеер только с OpenGL ES 3, грузит те же
// переделанные бандлы, что пойдут на Свитч (шейдеры GLES3). Android -- то, что
// запускает загрузчик ultrakill_nx: ARM64, IL2CPP, только GLES3, symbols.zip.
using System.IO;
using UnityEditor;
using UnityEditor.Build;
using UnityEngine;
using UnityEngine.Rendering;

public static class NxBuild
{
    const string Scene = "Assets/Scenes/Bootstrap.unity";

    static void Common()
    {
        PlayerSettings.companyName = "NxPort";
        PlayerSettings.productName = "ULTRAKILL NX";
        PlayerSettings.bundleVersion = "1.0";
        PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Standalone, ApiCompatibilityLevel.NET_Unity_4_8);
        PlayerSettings.SetApiCompatibilityLevel(NamedBuildTarget.Android, ApiCompatibilityLevel.NET_Unity_4_8);
        PlayerSettings.SetManagedStrippingLevel(NamedBuildTarget.Android, ManagedStrippingLevel.Minimal);
        PlayerSettings.stripEngineCode = false;
    }

    static void Report(UnityEditor.Build.Reporting.BuildReport r)
    {
        Debug.Log($"[nxbuild] {r.summary.platform}: {r.summary.result}, {r.summary.totalSize / 1048576} MB, " +
                  $"errors {r.summary.totalErrors}, {r.summary.totalTime}");
        if (r.summary.result != UnityEditor.Build.Reporting.BuildResult.Succeeded)
            EditorApplication.Exit(1);
    }

    public static void WindowsGles()
    {
        Common();
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.StandaloneWindows64, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.StandaloneWindows64, new[] { GraphicsDeviceType.OpenGLES3 });
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Standalone, ScriptingImplementation.Mono2x);
        PlayerSettings.fullScreenMode = FullScreenMode.Windowed;
        PlayerSettings.defaultScreenWidth = 1280;
        PlayerSettings.defaultScreenHeight = 720;
        Report(BuildPipeline.BuildPlayer(new[] { Scene }, "Build/Windows/ULTRAKILL NX.exe",
                                         BuildTarget.StandaloneWindows64, BuildOptions.Development));
    }

    public static void Android()
    {
        Common();
        EditorUserBuildSettings.SwitchActiveBuildTarget(BuildTargetGroup.Android, BuildTarget.Android);
        PlayerSettings.SetApplicationIdentifier(NamedBuildTarget.Android, "com.ultrakillnx.port");
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Android, ScriptingImplementation.IL2CPP);
        PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.OpenGLES3 });
        PlayerSettings.Android.optimizedFramePacing = false;   // Swappy не нужен: Choreographer'а нет
        PlayerSettings.Android.useAPKExpansionFiles = false;
        PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
        EditorUserBuildSettings.androidCreateSymbols = AndroidCreateSymbols.Debugging;
        EditorUserBuildSettings.androidBuildSubtarget = MobileTextureSubtarget.DXT;  // Tegra понимает DXT
        EditorUserBuildSettings.buildAppBundle = false;
        Report(BuildPipeline.BuildPlayer(new[] { Scene }, "Build/Android/ultrakill.apk",
                                         BuildTarget.Android, BuildOptions.None));
    }

    // То же, но экспорт Gradle-проекта: Gradle и лицензии SDK не нужны, IL2CPP
    // собирает libil2cpp.so через NDK. Загрузчику нужны только библиотеки и
    // assets из unityLibrary/src/main, а основные данные всё равно берутся из игры.
    public static void AndroidExport()
    {
        EditorUserBuildSettings.exportAsGoogleAndroidProject = true;
        Common();
        EditorUserBuildSettings.SwitchActiveBuildTarget(BuildTargetGroup.Android, BuildTarget.Android);
        PlayerSettings.SetApplicationIdentifier(NamedBuildTarget.Android, "com.ultrakillnx.port");
        PlayerSettings.SetScriptingBackend(NamedBuildTarget.Android, ScriptingImplementation.IL2CPP);
        PlayerSettings.Android.targetArchitectures = AndroidArchitecture.ARM64;
        PlayerSettings.SetUseDefaultGraphicsAPIs(BuildTarget.Android, false);
        PlayerSettings.SetGraphicsAPIs(BuildTarget.Android, new[] { GraphicsDeviceType.OpenGLES3 });
        PlayerSettings.Android.optimizedFramePacing = false;
        PlayerSettings.Android.useAPKExpansionFiles = false;
        PlayerSettings.Android.minSdkVersion = AndroidSdkVersions.AndroidApiLevel26;
        EditorUserBuildSettings.androidCreateSymbols = AndroidCreateSymbols.Debugging;
        EditorUserBuildSettings.androidBuildSubtarget = MobileTextureSubtarget.DXT;
        Report(BuildPipeline.BuildPlayer(new[] { Scene }, "Build/AndroidExport",
                                         BuildTarget.Android, BuildOptions.None));
    }
}
