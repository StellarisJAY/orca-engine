#include "window.h"

void Window::register_event_callback(const EventCallback &callback)
{
    callbacks.push_back(callback);
}

void Window::emit(const WindowEvent &event) const
{
    for(auto cb : callbacks)
    {
        cb(event);
    }
}