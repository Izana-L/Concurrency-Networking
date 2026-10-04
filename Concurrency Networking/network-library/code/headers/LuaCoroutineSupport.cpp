#include "pch.h"
#include "LuaCoroutineSupport.h"
#if __has_include("LuaCoroutineSupport.g.cpp")
#include "LuaCoroutineSupport.g.cpp"
#endif

using namespace winrt;
using namespace Windows::UI::Xaml;

namespace winrt::networklibrary::implementation
{
    int32_t LuaCoroutineSupport::MyProperty()
    {
        throw hresult_not_implemented();
    }

    void LuaCoroutineSupport::MyProperty(int32_t /* value */)
    {
        throw hresult_not_implemented();
    }

    void LuaCoroutineSupport::ClickHandler(IInspectable const&, RoutedEventArgs const&)
    {
        Button().Content(box_value(L"Clicked"));
    }
}
