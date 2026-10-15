// Copyright (c) 2026 Y.Nakaue. MIT License.
//
// ホストに必要なSDK実装を、このファイルでまとめてコンパイルする。

#if AUDIOPLUGINHOST_WITH_VST3SDK
#if PLATFORM_WINDOWS
// SDK由来の警告はここで抑え、上流のコードは変更しない。
#pragma warning(push)
#pragma warning(disable : 4191 4777 4996)
#endif
#include "pluginterfaces/base/funknown.cpp"
#include "pluginterfaces/base/coreiids.cpp"
#include "pluginterfaces/base/conststringtable.cpp"
#include "pluginterfaces/base/ustring.cpp"
#include "public.sdk/source/common/commonstringconvert.cpp"
#include "public.sdk/source/common/commoniids.cpp"
#include "public.sdk/source/common/memorystream.cpp"
#include "public.sdk/source/common/threadchecker_win32.cpp"
#include "public.sdk/source/vst/utility/stringconvert.cpp"
#include "public.sdk/source/vst/vstinitiids.cpp"
#include "public.sdk/source/vst/hosting/connectionproxy.cpp"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.cpp"
#include "public.sdk/source/vst/hosting/hostclasses.cpp"
#include "public.sdk/source/vst/hosting/plugprovider.cpp"
#include "public.sdk/source/vst/hosting/module.cpp"
#include "public.sdk/source/vst/hosting/module_win32.cpp"
#if PLATFORM_WINDOWS
#pragma warning(pop)
#endif
#endif
