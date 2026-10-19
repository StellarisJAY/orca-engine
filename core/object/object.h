#pragma once

enum{
    NOTIFICATION_ENTER_TREE,
    NOTIFICATION_EXIT_TREE,
    NOTIFICATION_TRANSFORM_CHANGE,
    NOTIFICATION_VISIBILITY_CHANGE,
    NOTIFICATION_ACTIVE_CHANGE,
};

class Object
{
public:
    // 虚析构是必需的：通过 Object* 调用 memdelete 时要能派发到派生类析构。
    virtual ~Object() = default;
    virtual void _notification(int what) {}
};