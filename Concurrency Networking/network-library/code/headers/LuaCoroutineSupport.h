#pragma once

#include "LuaCoroutineSupport.g.h"

namespace winrt::networklibrary::implementation
{
    struct LuaCoroutineSupport : LuaCoroutineSupportT<LuaCoroutineSupport>
    {
        LuaCoroutineSupport() 
        {
            // Xaml objects should not call InitializeComponent during construction.
            // See https://github.com/microsoft/cppwinrt/tree/master/nuget#initializecomponent
        }

        int32_t MyProperty();
        void MyProperty(int32_t value);

        void ClickHandler(Windows::Foundation::IInspectable const& sender, Windows::UI::Xaml::RoutedEventArgs const& args);
    };
}

namespace winrt::networklibrary::factory_implementation
{
    struct LuaCoroutineSupport : LuaCoroutineSupportT<LuaCoroutineSupport, implementation::LuaCoroutineSupport>
    {
    };
}
