# UnityResolve.hpp
> ### 提供的Unity类型
> - [ ] Camera
> - [ ] Transform
> - [ ] GameObject
> - [ ] Component
> - [ ] LayerMask
> - [ ] Rigidbody
> - [X] Vector4
> - [X] Vector3
> - [X] Vector2
> - [X] Quaternion
> - [X] Bounds
> - [X] Plane
> - [X] Ray
> - [X] Rect
> - [X] Color
> - [X] Matrix4x4
> - [ ] Array
> - [x] String
> - [x] Object
> - [ ] List
> - More...

> ### 待完成功能
> - [X] DumpToFile
> - [ ] 修改静态变量值
> - [ ] 获取实例
> - More...

> [!NOTE]\
> 有新的功能建议或者Bug可以提交Issues

#### 初始化
``` c++
UnityResolve::Init(GetModuleHandle(L"GameAssembly.dll"), UnityResolve::Mode::Il2cpp);
```
> 参数1: dll句柄 \
> 参数2: 使用模式
> > Mode::Il2cpp \
> > Mode::Mono

#### 获取函数地址及调用
``` c++
const auto classes = UnityResolve::assembly["程序集名称"]->classes;
auto klass = classes.at("类名称"); // 我不知道为什么在这里使用[]会提示没有重载 std::map<std::string, Class*> classes

const auto field = klass->Get<UnityResolve::Field>("Field");
const auto method = klass->Get<UnityResolve::Method>("Method");

const auto functionPtr = method->function;

const auto method1 = klass->methods["函数名称1"];
const auto method2 = klass->methods["函数名称2"];

method1->Invoke<int>(114, 514);

const auto ptr = method2->Cast<void, int, bool>();
ptr(114514, true);
```
