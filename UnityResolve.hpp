#ifndef UNITYRESOLVE_HPP
#define UNITYRESOLVE_HPP

// ============================== 自动检测当前环境 ==============================

#if defined(_WIN32) || defined(_WIN64)
#define WINDOWS_MODE 1
#else
#define WINDOWS_MODE 0
#endif

#if defined(__ANDROID__)
#define ANDROID_MODE 1
#else
#define ANDROID_MODE 0
#endif

#if defined(TARGET_OS_IOS)
#define IOS_MODE 1
#else
#define IOS_MODE 0
#endif

#if defined(__linux__) && !defined(__ANDROID__)
#define LINUX_MODE 1
#else
#define LINUX_MODE 0
#endif

#if defined(__harmony__) && !defined(_HARMONYOS)
#define HARMONYOS_MODE 1
#else
#define HARMONYOS_MODE 0
#endif

// ============================== 强制设置当前执行环境 ==============================

// #define WINDOWS_MODE 0
// #define ANDROID_MODE 1 // 设置运行环境
// #define LINUX_MODE 0
// #define IOS_MODE 0
// #define HARMONYOS_MODE 0

// ============================== 导入对应环境依赖 ==============================

#if WINDOWS_MODE || LINUX_MODE || IOS_MODE
#include <format>
#include <ranges>
#include <regex>
#endif

#if IOS_MODE
#include <algorithm>
#endif

#include <codecvt>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <numbers>

#ifdef USE_GLM
#include <glm/glm.hpp>
#endif

#if WINDOWS_MODE
#include <windows.h>
#undef GetObject
#endif

#if WINDOWS_MODE
#ifdef _WIN64
#define UNITY_CALLING_CONVENTION __fastcall
#elif _WIN32
#define UNITY_CALLING_CONVENTION __cdecl
#endif
#elif ANDROID_MODE || LINUX_MODE || IOS_MODE || HARMONYOS_MODE
#include <locale>
#include <dlfcn.h>
#define UNITY_CALLING_CONVENTION
#endif

class UnityResolve final {
public:
	struct Assembly;
	struct Type;
	struct Class;
	struct Field;
	struct Method;
	class UnityType;

	enum class Mode : char {
		Il2Cpp,
		Mono,
	};

	struct Assembly final {
		void* address;
		std::string         name;
		std::string         file;
		std::vector<Class*> classes;

		[[nodiscard]] auto Get(const std::string& strClass, const std::string& strNamespace = "*", const std::string& strParent = "*") const -> Class* {

			for (const auto pClass : classes) if (strClass == pClass->name && (strNamespace == "*" || pClass->namespaze == strNamespace) && (strParent == "*" || pClass->parent == strParent)) return pClass;
			return nullptr;
		}
	};

	struct Type final {
		void* address;
		std::string name;
		int         size;

		// UnityType::CsType*
		[[nodiscard]] auto GetCSType() const -> void* {
			if (mode_ == Mode::Il2Cpp) return Invoke<void*>("il2cpp_type_get_object", address);
			return Invoke<void*>("mono_type_get_object", pDomain, address);
		}
	};

	struct Class final {
		void* address;
		std::string name;
		std::string parent;
		std::string namespaze;
		std::vector<Field*>  fields;
		std::vector<Method*> methods;
		void* objType;

		template <typename RType>
		auto Get(const std::string& name, const std::vector<std::string>& args = {}) -> RType* {

			if constexpr (std::is_same_v<RType, Field>) for (auto pField : fields) if (pField->name == name) return static_cast<RType*>(pField);
			if constexpr (std::is_same_v<RType, std::int32_t>) for (const auto pField : fields) if (pField->name == name) return reinterpret_cast<RType*>(pField->offset);
			if constexpr (std::is_same_v<RType, Method>) {
				for (auto pMethod : methods) {
					if (pMethod->name == name) {
						if (pMethod->args.empty() && args.empty()) return static_cast<RType*>(pMethod);
						if (pMethod->args.size() == args.size()) {
							size_t index{ 0 };
							for (size_t i{ 0 }; const auto & typeName : args) if (typeName == "*" || typeName.empty() ? true : pMethod->args[i++]->pType->name == typeName) index++;
							if (index == pMethod->args.size()) return static_cast<RType*>(pMethod);
						}
					}
				}

				for (auto pMethod : methods) if (pMethod->name == name) return static_cast<RType*>(pMethod);
			}
			return nullptr;
		}

		template <typename RType>
		auto GetValue(void* obj, const std::string& name) -> RType { return *reinterpret_cast<RType*>(reinterpret_cast<uintptr_t>(obj) + Get<Field>(name)->offset); }

		template <typename RType>
		auto GetValue(void* obj, unsigned int offset) -> RType { return *reinterpret_cast<RType*>(reinterpret_cast<uintptr_t>(obj) + offset); }

		template <typename RType>
		auto SetValue(void* obj, const std::string& name, RType value) -> void { *reinterpret_cast<RType*>(reinterpret_cast<uintptr_t>(obj) + Get<Field>(name)->offset) = value; }

		template <typename RType>
		auto SetValue(void* obj, unsigned int offset, RType value) -> RType { *reinterpret_cast<RType*>(reinterpret_cast<uintptr_t>(obj) + offset) = value; }

		// UnityType::CsType*
		[[nodiscard]] auto GetType() -> void* {
			if (objType) return objType;
			if (mode_ == Mode::Il2Cpp) {
				const auto pUType = Invoke<void*, void*>("il2cpp_class_get_type", address);
				objType = Invoke<void*>("il2cpp_type_get_object", pUType);
				return objType;
			}
			const auto pUType = Invoke<void*, void*>("mono_class_get_type", address);
			objType = Invoke<void*>("mono_type_get_object", pDomain, pUType);
			return objType;
		}

		/**
		 * \brief 获取类所有实例
		 * \tparam T 返回数组类型
		 * \param type 类
		 * \return 返回实例指针数组
		 */
		template <typename T>
		auto FindObjectsByType() -> std::vector<T> {
			static Method* pMethod;

			if (!pMethod) pMethod = UnityResolve::Get("UnityEngine.CoreModule.dll")->Get("Object")->Get<Method>("FindObjectsOfType", { "System.Type" });
			if (!objType) objType = this->GetType();

			if (pMethod && objType) if (auto array = pMethod->Invoke<UnityType::Array<T>*>(objType)) return array->ToVector();

			return std::vector<T>(0);
		}

		template <typename T>
		auto New() -> T* {
			if (mode_ == Mode::Il2Cpp) return Invoke<T*, void*>("il2cpp_object_new", address);
			return Invoke<T*, void*, void*>("mono_object_new", pDomain, address);
		}
	};

	struct Field final {
		void* address;
		std::string name;
		Type* type;
		Class* klass;
		std::int32_t offset; // If offset is -1, then it's thread static
		bool static_field;
		void* vTable;

		template <typename T>
		auto SetStaticValue(T* value) const -> void {
			if (!static_field) return;
			if (mode_ == Mode::Il2Cpp) return Invoke<void, void*, T*>("il2cpp_field_static_set_value", address, value);
			else
			{
				void* VTable = Invoke<void*>("mono_class_vtable", pDomain, klass->address);
				return Invoke<void, void*, void*, T*>("mono_field_static_set_value", VTable, address, value);

			}
		}

		template <typename T>
		auto GetStaticValue(T* value) const -> void {
			if (!static_field) return;
			if (mode_ == Mode::Il2Cpp) return Invoke<void, void*, T*>("il2cpp_field_static_get_value", address, value);
			else
			{
				void* VTable = Invoke<void*>("mono_class_vtable", pDomain, klass->address);
				return Invoke<void, void*, void*, T*>("mono_field_static_get_value", VTable, address, value);
			}
		}

		template <typename T, typename C>
		struct Variable {
		private:
			std::int32_t offset{ 0 };

		public:
			void Init(const Field* field) {
				offset = field->offset;
			}

			T Get(C* obj) {
				return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + offset);
			}

			void Set(C* obj, T value) {
				*reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(obj) + offset) = value;
			}

			T& operator[](C* obj) {
				return *reinterpret_cast<T*>(offset + reinterpret_cast<std::uintptr_t>(obj));
			}
		};
	};

	template <typename Return, typename... Args>
	using MethodPointer = Return(UNITY_CALLING_CONVENTION*)(Args...);

	struct Method final {
		void* address;
		std::string  name;
		Class* klass;
		Type* return_type;
		std::int32_t flags;
		bool         static_function;
		void* function;

		struct Arg {
			std::string name;
			Type* pType;
		};

		std::vector<Arg*> args;
	public:
		template <typename Return, typename... Args>
		auto Invoke(Args... args) -> Return {

			Compile();
			if (function) return reinterpret_cast<Return(UNITY_CALLING_CONVENTION*)(Args...)>(function)(args...);
			return Return();
		}

		auto Compile() -> void {

			if (address && !function && mode_ == Mode::Mono) function = UnityResolve::Invoke<void*>("mono_compile_method", address);
		}

		template <typename Return, typename Obj = void, typename... Args>
		auto RuntimeInvoke(Obj* obj, Args... args) -> Return {
			void* argArray[sizeof...(Args)] = { static_cast<void*>(&args)... };

			if (mode_ == Mode::Il2Cpp) {
				if constexpr (std::is_void_v<Return>) {
					UnityResolve::Invoke<void*>("il2cpp_runtime_invoke", address, obj, sizeof...(Args) ? argArray : nullptr, nullptr);
					return;
				}
				else return Unbox<Return>(Invoke<void*>("il2cpp_runtime_invoke", address, obj, sizeof...(Args) ? argArray : nullptr, nullptr));
			}

			if constexpr (std::is_void_v<Return>) {
				UnityResolve::Invoke<void*>("mono_runtime_invoke", address, obj, sizeof...(Args) ? argArray : nullptr, nullptr);
				return;
			}
			else return Unbox<Return>(Invoke<void*>("mono_runtime_invoke", address, obj, sizeof...(Args) ? argArray : nullptr, nullptr));
		}

		template <typename Return, typename... Args>
		auto Cast() -> MethodPointer<Return, Args...> {

			Compile();
			if (function) return reinterpret_cast<MethodPointer<Return, Args...>>(function);
			return nullptr;
		}

		template <typename Return, typename... Args>
		auto Cast(MethodPointer<Return, Args...>& ptr) -> MethodPointer<Return, Args...> {

			Compile();
			if (function) {
				ptr = reinterpret_cast<MethodPointer<Return, Args...>>(function);
				return reinterpret_cast<MethodPointer<Return, Args...>>(function);
			}
			return nullptr;
		}

		template <typename Return, typename... Args>
		auto Cast(std::function<Return(Args...)>& ptr) -> std::function<Return(Args...)> {

			Compile();
			if (function) {
				ptr = reinterpret_cast<MethodPointer<Return, Args...>>(function);
				return reinterpret_cast<MethodPointer<Return, Args...>>(function);
			}
			return nullptr;
		}

		template <typename T>
		T Unbox(void* obj) {
			if (mode_ == Mode::Il2Cpp) {
				return static_cast<T>(Invoke<void*>("mono_object_unbox", obj));
			}
			else {
				return static_cast<T>(Invoke<void*>("il2cpp_object_unbox", obj));
			}
		}
	};

	class AssemblyLoad {
	public:
		AssemblyLoad(const std::string path, std::string namespaze = "", std::string className = "", std::string desc = "") {
			if (mode_ == Mode::Mono) {
				assembly = Invoke<void*>("mono_domain_assembly_open", pDomain, path.data());
				image = Invoke<void*>("mono_assembly_get_image", assembly);
				if (namespaze.empty() || className.empty() || desc.empty()) {
					return;
				}
				klass = Invoke<void*>("mono_class_from_name", image, namespaze.data(), className.data());
				void* entry_point_method_desc = Invoke<void*>("mono_method_desc_new", desc.data(), true);
				method = Invoke<void*>("mono_method_desc_search_in_class", entry_point_method_desc, klass);
				Invoke<void>("mono_method_desc_free", entry_point_method_desc);
				Invoke<void*>("mono_runtime_invoke", method, nullptr, nullptr, nullptr);
			}
		}

		void* assembly;
		void* image;
		void* klass;
		void* method;
	};

	static auto ThreadAttach() -> void {
		if (mode_ == Mode::Il2Cpp) Invoke<void*>("il2cpp_thread_attach", pDomain);
		else {
			Invoke<void*>("mono_thread_attach", pDomain);
			Invoke<void*>("mono_jit_thread_attach", pDomain);
		}
	}

	static auto ThreadDetach() -> void {
		if (mode_ == Mode::Il2Cpp) Invoke<void*>("il2cpp_thread_detach", pDomain);
		else {
			Invoke<void*>("mono_thread_detach", pDomain);
			Invoke<void*>("mono_jit_thread_detach", pDomain);
		}
	}

	static auto Init(void* hmodule, const Mode mode = Mode::Mono) -> void {
		mode_ = mode;
		hmodule_ = hmodule;

		if (mode_ == Mode::Il2Cpp) {
			pDomain = Invoke<void*>("il2cpp_domain_get");
			Invoke<void*>("il2cpp_thread_attach", pDomain);
			ForeachAssembly();
		}
		else {
			pDomain = Invoke<void*>("mono_get_root_domain");
			Invoke<void*>("mono_thread_attach", pDomain);
			Invoke<void*>("mono_jit_thread_attach", pDomain);

			ForeachAssembly();
		}
	}

#if WINDOWS_MODE || LINUX_MODE || IOS_MODE /*__cplusplus >= 202002L*/
	static auto DumpToFile(const std::string path) -> void {
		std::ofstream io(path + "dump.cs", std::fstream::out);
		if (!io) return;

		for (const auto& pAssembly : assembly) {
			for (const auto& pClass : pAssembly->classes) {
				io << std::format("\tnamespace: {}", pClass->namespaze.empty() ? "" : pClass->namespaze);
				io << "\n";
				io << std::format("\tAssembly: {}\n", pAssembly->name.empty() ? "" : pAssembly->name);
				io << std::format("\tAssemblyFile: {} \n", pAssembly->file.empty() ? "" : pAssembly->file);
				io << std::format("\tclass {}{} ", pClass->name, pClass->parent.empty() ? "" : " : " + pClass->parent);
				io << "{\n\n";
				for (const auto& pField : pClass->fields) io << std::format("\t\t{:+#06X} | {}{} {};\n", pField->offset, pField->static_field ? "static " : "", pField->type->name, pField->name);
				io << "\n";
				for (const auto& pMethod : pClass->methods) {
					io << std::format("\t\t[Flags: {:032b}] [ParamsCount: {:04d}] |RVA: {:+#010X}|\n", pMethod->flags, pMethod->args.size(), reinterpret_cast<std::uint64_t>(pMethod->function) - reinterpret_cast<std::uint64_t>(hmodule_));
					io << std::format("\t\t{}{} {}(", pMethod->static_function ? "static " : "", pMethod->return_type->name, pMethod->name);
					std::string params{};
					for (const auto& pArg : pMethod->args) params += std::format("{} {}, ", pArg->pType->name, pArg->name);
					if (!params.empty()) {
						params.pop_back();
						params.pop_back();
					}
					io << (params.empty() ? "" : params) << ");\n\n";
				}
				io << "\t}\n\n";
			}
		}

		io << '\n';
		io.close();

		std::ofstream io2(path + "struct.hpp", std::fstream::out);
		if (!io2) return;

		for (const auto& pAssembly : assembly) {
			for (const auto& pClass : pAssembly->classes) {
				io2 << std::format("\tnamespace: {}", pClass->namespaze.empty() ? "" : pClass->namespaze);
				io2 << "\n";
				io2 << std::format("\tAssembly: {}\n", pAssembly->name.empty() ? "" : pAssembly->name);
				io2 << std::format("\tAssemblyFile: {} \n", pAssembly->file.empty() ? "" : pAssembly->file);
				io2 << std::format("\tstruct {}{} ", pClass->name, pClass->parent.empty() ? "" : " : " + pClass->parent);
				io2 << "{\n\n";

				for (size_t i = 0; i < pClass->fields.size(); i++) {
					if (pClass->fields[i]->static_field) continue;

					auto field = pClass->fields[i];

				next: if ((i + 1) >= pClass->fields.size()) {
					io2 << std::format("\t\tchar {}[0x{:06X}];\n", field->name, 0x4);
					continue;
				}

				if (pClass->fields[i + 1]->static_field) {
					i++;
					goto next;
				}

				std::string name = field->name;
				std::ranges::replace(name, '<', '_');
				std::ranges::replace(name, '>', '_');

				if (field->type->name == "System.Int64") {
					io2 << std::format("\t\tstd::int64_t {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 8) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 8);
					continue;
				}

				if (field->type->name == "System.UInt64") {
					io2 << std::format("\t\tstd::uint64_t {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 8) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 8);
					continue;
				}

				if (field->type->name == "System.Int32") {
					io2 << std::format("\t\tint {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 4) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 4);
					continue;
				}

				if (field->type->name == "System.UInt32") {
					io2 << std::format("\t\tstd::uint32_t {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 4) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 4);
					continue;
				}

				if (field->type->name == "System.Boolean") {
					io2 << std::format("\t\tbool {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 1) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 1);
					continue;
				}

				if (field->type->name == "System.String") {
					io2 << std::format("\t\tUnityResolve::UnityType::String* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				if (field->type->name == "System.Single") {
					io2 << std::format("\t\tfloat {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 4) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 4);
					continue;
				}

				if (field->type->name == "System.Double") {
					io2 << std::format("\t\tdouble {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > 8) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - 8);
					continue;
				}

				if (field->type->name == "UnityEngine.Vector3") {
					io2 << std::format("\t\tUnityResolve::UnityType::Vector3 {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Vector3)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Vector3));
					continue;
				}

				if (field->type->name == "UnityEngine.Vector2") {
					io2 << std::format("\t\tUnityResolve::UnityType::Vector2 {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Vector2)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Vector2));
					continue;
				}

				if (field->type->name == "UnityEngine.Vector4") {
					io2 << std::format("\t\tUnityResolve::UnityType::Vector4 {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Vector4)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Vector4));
					continue;
				}

				if (field->type->name == "UnityEngine.GameObject") {
					io2 << std::format("\t\tUnityResolve::UnityType::GameObject* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				if (field->type->name == "UnityEngine.Transform") {
					io2 << std::format("\t\tUnityResolve::UnityType::Transform* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				if (field->type->name == "UnityEngine.Animator") {
					io2 << std::format("\t\tUnityResolve::UnityType::Animator* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				if (field->type->name == "UnityEngine.Physics") {
					io2 << std::format("\t\tUnityResolve::UnityType::Physics* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				if (field->type->name == "UnityEngine.Component") {
					io2 << std::format("\t\tUnityResolve::UnityType::Component* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				if (field->type->name == "UnityEngine.Rect") {
					io2 << std::format("\t\tUnityResolve::UnityType::Rect {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Rect)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Rect));
					continue;
				}

				if (field->type->name == "UnityEngine.Quaternion") {
					io2 << std::format("\t\tUnityResolve::UnityType::Quaternion {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Quaternion)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Quaternion));
					continue;
				}

				if (field->type->name == "UnityEngine.Color") {
					io2 << std::format("\t\tUnityResolve::UnityType::Color {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Color)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Color));
					continue;
				}

				if (field->type->name == "UnityEngine.Matrix4x4") {
					io2 << std::format("\t\tUnityResolve::UnityType::Matrix4x4 {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(UnityType::Matrix4x4)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(UnityType::Matrix4x4));
					continue;
				}

				if (field->type->name == "UnityEngine.Rigidbody") {
					io2 << std::format("\t\tUnityResolve::UnityType::Rigidbody* {};\n", name);
					if (!pClass->fields[i + 1]->static_field && (pClass->fields[i + 1]->offset - field->offset) > sizeof(void*)) io2 << std::format("\t\tchar {}_[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset - sizeof(void*));
					continue;
				}

				io2 << std::format("\t\tchar {}[0x{:06X}];\n", name, pClass->fields[i + 1]->offset - field->offset);
				}

				io2 << "\n";
				io2 << "\t};\n\n";
			}
		}
		io2 << '\n';
		io2.close();
	}
#endif

	template <typename Return, typename... Args>
	static auto Invoke(const std::string& funcName, Args... args) -> Return {
#if WINDOWS_MODE
		if (!address_.contains(funcName) || !address_[funcName]) address_[funcName] = static_cast<void*>(GetProcAddress(static_cast<HMODULE>(hmodule_), funcName.c_str()));
#elif  ANDROID_MODE || LINUX_MODE || IOS_MODE || HARMONYOS_MODE
		if (address_.find(funcName) == address_.end() || !address_[funcName]) {
			address_[funcName] = dlsym(hmodule_, funcName.c_str());
		}
#endif

		if (address_[funcName] != nullptr) {
			try {
				return reinterpret_cast<Return(UNITY_CALLING_CONVENTION*)(Args...)>(address_[funcName])(args...);
			}
			catch (...) {
				return Return();
			}
		}
		return Return();
	}

	inline static std::vector<Assembly*> assembly;

	static auto Get(const std::string& strAssembly) -> Assembly* {
		for (const auto pAssembly : assembly) if (pAssembly->name == strAssembly) return pAssembly;
		return nullptr;
	}

private:
	static auto ForeachAssembly() -> void {
		// 遍历程序集
		if (mode_ == Mode::Il2Cpp) {
			size_t     nrofassemblies = 0;
			const auto assemblies = Invoke<void**>("il2cpp_domain_get_assemblies", pDomain, &nrofassemblies);
			for (auto i = 0; i < nrofassemblies; i++) {
				const auto ptr = assemblies[i];
				if (ptr == nullptr) continue;
				auto       assembly = new Assembly{ .address = ptr };
				const auto image = Invoke<void*>("il2cpp_assembly_get_image", ptr);
				assembly->file = Invoke<const char*>("il2cpp_image_get_filename", image);
				assembly->name = Invoke<const char*>("il2cpp_image_get_name", image);
				UnityResolve::assembly.push_back(assembly);
				ForeachClass(assembly, image);
			}
		}
		else {
			Invoke<void*, void(*)(void* ptr, std::vector<Assembly*>&), std::vector<Assembly*>&>("mono_assembly_foreach",
				[](void* ptr, std::vector<Assembly*>& v) {
					if (ptr == nullptr) return;

					auto assembly = new Assembly{ .address = ptr };
					void* image;
					try {
						image = Invoke<void*>("mono_assembly_get_image", ptr);
						assembly->file = Invoke<const char*>("mono_image_get_filename", image);
						assembly->name = Invoke<const char*>("mono_image_get_name", image);
						assembly->name += ".dll";
						v.push_back(assembly);
					}
					catch (...) {
						return;
					}

					ForeachClass(assembly, image);
				},
				assembly);
		}
	}

	static auto ForeachClass(Assembly* assembly, void* image) -> void {
		// 遍历类
		if (mode_ == Mode::Il2Cpp) {
			const auto count = Invoke<int>("il2cpp_image_get_class_count", image);
			for (auto i = 0; i < count; i++) {
				const auto pClass = Invoke<void*>("il2cpp_image_get_class", image, i);
				if (pClass == nullptr) continue;
				const auto pAClass = new Class();
				pAClass->address = pClass;
				pAClass->name = Invoke<const char*>("il2cpp_class_get_name", pClass);
				if (const auto pPClass = Invoke<void*>("il2cpp_class_get_parent", pClass)) pAClass->parent = Invoke<const char*>("il2cpp_class_get_name", pPClass);
				pAClass->namespaze = Invoke<const char*>("il2cpp_class_get_namespace", pClass);
				assembly->classes.push_back(pAClass);

				ForeachFields(pAClass, pClass);
				ForeachMethod(pAClass, pClass);

				void* i_class;
				void* iter{};
				do {
					if ((i_class = Invoke<void*>("il2cpp_class_get_interfaces", pClass, &iter))) {
						ForeachFields(pAClass, i_class);
						ForeachMethod(pAClass, i_class);
					}
				} while (i_class);
			}
		}
		else {
			try {
				const void* table = Invoke<void*>("mono_image_get_table_info", image, 2);
				const auto  count = Invoke<int>("mono_table_info_get_rows", table);
				for (auto i = 0; i < count; i++) {
					const auto pClass = Invoke<void*>("mono_class_get", image, 0x02000000 | (i + 1));
					if (pClass == nullptr) continue;

					const auto pAClass = new Class();
					pAClass->address = pClass;
					try {
						pAClass->name = Invoke<const char*>("mono_class_get_name", pClass);
						if (const auto pPClass = Invoke<void*>("mono_class_get_parent", pClass)) pAClass->parent = Invoke<const char*>("mono_class_get_name", pPClass);
						pAClass->namespaze = Invoke<const char*>("mono_class_get_namespace", pClass);
						assembly->classes.push_back(pAClass);
					}
					catch (...) {
						return;
					}

					ForeachFields(pAClass, pClass);
					ForeachMethod(pAClass, pClass);

					void* iClass;
					void* iiter{};

					do {
						try {
							if ((iClass = Invoke<void*>("mono_class_get_interfaces", pClass, &iiter))) {
								ForeachFields(pAClass, iClass);
								ForeachMethod(pAClass, iClass);
							}
						}
						catch (...) {
							return;
						}
					} while (iClass);
				}
			}
			catch (...) {}
		}
	}

	static auto ForeachFields(Class* klass, void* pKlass) -> void {
		// 遍历成员
		if (mode_ == Mode::Il2Cpp) {
			void* iter = nullptr;
			void* field;
			do {
				if ((field = Invoke<void*>("il2cpp_class_get_fields", pKlass, &iter))) {
					const auto pField = new Field{ .address = field, .name = Invoke<const char*>("il2cpp_field_get_name", field), .type = new Type{.address = Invoke<void*>("il2cpp_field_get_type", field)}, .klass = klass, .offset = Invoke<int>("il2cpp_field_get_offset", field), .static_field = false, .vTable = nullptr };
					pField->static_field = pField->offset <= 0;
					pField->type->name = Invoke<const char*>("il2cpp_type_get_name", pField->type->address);
					pField->type->size = -1;
					klass->fields.push_back(pField);
				}
			} while (field);
		}
		else {
			void* iter = nullptr;
			void* field;
			do {
				try {
					if ((field = Invoke<void*>("mono_class_get_fields", pKlass, &iter))) {
						const auto pField = new Field{ .address = field, .name = Invoke<const char*>("mono_field_get_name", field), .type = new Type{.address = Invoke<void*>("mono_field_get_type", field)}, .klass = klass, .offset = Invoke<int>("mono_field_get_offset", field), .static_field = false, .vTable = nullptr };
						int        tSize{};
						/*pField->static_field = pField->offset <= 0;*/
						int flags = Invoke<int>("mono_field_get_flags", field);
						if (flags & 0x10)//0x10=FIELD_ATTRIBUTE_STATIC
						{
							pField->static_field = true;
						}
						pField->type->name = Invoke<const char*>("mono_type_get_name", pField->type->address);
						pField->type->size = Invoke<int>("mono_type_size", pField->type->address, &tSize);
						klass->fields.push_back(pField);
					}
				}
				catch (...) {
					return;
				}
			} while (field);
		}
	}

	static auto ForeachMethod(Class* klass, void* pKlass) -> void {
		// 遍历方法
		if (mode_ == Mode::Il2Cpp) {
			void* iter = nullptr;
			void* method;
			do {
				if ((method = Invoke<void*>("il2cpp_class_get_methods", pKlass, &iter))) {
					int        fFlags{};
					const auto pMethod = new Method{};
					pMethod->address = method;
					pMethod->name = Invoke<const char*>("il2cpp_method_get_name", method);
					pMethod->klass = klass;
					pMethod->return_type = new Type{ .address = Invoke<void*>("il2cpp_method_get_return_type", method), };
					pMethod->flags = Invoke<int>("il2cpp_method_get_flags", method, &fFlags);

					pMethod->static_function = pMethod->flags & 0x10;
					pMethod->return_type->name = Invoke<const char*>("il2cpp_type_get_name", pMethod->return_type->address);
					pMethod->return_type->size = -1;
					pMethod->function = *static_cast<void**>(method);
					klass->methods.push_back(pMethod);
					const auto argCount = Invoke<int>("il2cpp_method_get_param_count", method);
					for (auto index = 0; index < argCount; index++) pMethod->args.push_back(new Method::Arg{ Invoke<const char*>("il2cpp_method_get_param_name", method, index), new Type{.address = Invoke<void*>("il2cpp_method_get_param", method, index), .name = Invoke<const char*>("il2cpp_type_get_name", Invoke<void*>("il2cpp_method_get_param", method, index)), .size = -1} });
				}
			} while (method);
		}
		else {
			void* iter = nullptr;
			void* method;

			do {
				try {
					if ((method = Invoke<void*>("mono_class_get_methods", pKlass, &iter))) {
						const auto signature = Invoke<void*>("mono_method_signature", method);
						if (!signature) continue;

						int fFlags{};
						const auto pMethod = new Method{};
						pMethod->address = method;

						char** names = nullptr;
						try {
							pMethod->name = Invoke<const char*>("mono_method_get_name", method);
							pMethod->klass = klass;
							pMethod->return_type = new Type{ .address = Invoke<void*>("mono_signature_get_return_type", signature) };

							pMethod->flags = Invoke<int>("mono_method_get_flags", method, &fFlags);
							pMethod->static_function = pMethod->flags & 0x10;

							pMethod->return_type->name = Invoke<const char*>("mono_type_get_name", pMethod->return_type->address);
							int tSize{};
							pMethod->return_type->size = Invoke<int>("mono_type_size", pMethod->return_type->address, &tSize);

							klass->methods.push_back(pMethod);

							int param_count = Invoke<int>("mono_signature_get_param_count", signature);
							names = new char* [param_count];
							Invoke<void>("mono_method_get_param_names", method, names);
						}
						catch (...) {
							continue;
						}

						void* mIter = nullptr;
						void* mType;
						int iname = 0;

						do {
							try {
								if ((mType = Invoke<void*>("mono_signature_get_params", signature, &mIter))) {
									int t_size{};
									try {
										pMethod->args.push_back(new Method::Arg{
											names[iname],
											new Type{.address = mType, .name = Invoke<const char*>("mono_type_get_name", mType), .size = Invoke<int>("mono_type_size", mType, &t_size) }
											});
									}
									catch (...) {

									}
									iname++;
								}
							}
							catch (...) {
								break;
							}
						} while (mType);
					}
				}
				catch (...) {
					return;
				}
			} while (method);
		}
	}

public:
	class UnityType final {
	public:
		using IntPtr = std::uintptr_t;
		using Int32 = std::int32_t;
		using Int64 = std::int64_t;
		using Char = wchar_t;
		using Int16 = std::int16_t;
		using Byte = std::uint8_t;
#ifndef USE_GLM
		struct Vector3;
		struct Vector4;
		struct Vector2;
		struct Quaternion;
		struct Matrix4x4;
#else
		using Vector3 = glm::vec3;
		using Vector2 = glm::vec2;
		using Vector4 = glm::vec4;
		using Quaternion = glm::quat;
		using Matrix4x4 = glm::mat4x4;
#endif
		struct Camera;
		struct Transform;
		struct Component;
		struct UnityObject;
		struct LayerMask;
		struct Rigidbody;
		struct Physics;
		struct GameObject;
		struct Collider;
		struct Bounds;
		struct Plane;
		struct Ray;
		struct Rect;
		struct Color;
		template <typename T>
		struct Array;
		struct String;
		struct Object;
		template <typename T>
		struct List;
		template <typename TKey, typename TValue>
		struct Dictionary;
		struct Behaviour;
		struct MonoBehaviour;
		struct CsType;
		struct Mesh;
		struct Renderer;
		struct Animator;
		struct CapsuleCollider;
		struct BoxCollider;
		struct FieldInfo;
		struct MethodInfo;
		struct PropertyInfo;
		struct Assembly;
		struct EventInfo;
		struct MemberInfo;
		struct Time;
		struct RaycastHit;

#ifndef USE_GLM
		struct Vector3 {
			float x, y, z;

			Vector3() { x = y = z = 0.f; }

			Vector3(const float f1, const float f2, const float f3) {
				x = f1;
				y = f2;
				z = f3;
			}

			[[nodiscard]] auto Length() const -> float { return x * x + y * y + z * z; }

			[[nodiscard]] auto Dot(const Vector3 b) const -> float { return x * b.x + y * b.y + z * b.z; }

			[[nodiscard]] auto Normalize() const -> Vector3 {
				if (const auto len = Length(); len > 0) return Vector3(x / len, y / len, z / len);
				return Vector3(x, y, z);
			}

			auto ToVectors(Vector3* m_pForward, Vector3* m_pRight, Vector3* m_pUp) const -> void {
				constexpr auto m_fDeg2Rad = std::numbers::pi_v<float> / 180.F;

				const auto m_fSinX = sinf(x * m_fDeg2Rad);
				const auto m_fCosX = cosf(x * m_fDeg2Rad);

				const auto m_fSinY = sinf(y * m_fDeg2Rad);
				const auto m_fCosY = cosf(y * m_fDeg2Rad);

				const auto m_fSinZ = sinf(z * m_fDeg2Rad);
				const auto m_fCosZ = cosf(z * m_fDeg2Rad);

				if (m_pForward) {
					m_pForward->x = m_fCosX * m_fCosY;
					m_pForward->y = -m_fSinX;
					m_pForward->z = m_fCosX * m_fSinY;
				}

				if (m_pRight) {
					m_pRight->x = -1.f * m_fSinZ * m_fSinX * m_fCosY + -1.f * m_fCosZ * -m_fSinY;
					m_pRight->y = -1.f * m_fSinZ * m_fCosX;
					m_pRight->z = -1.f * m_fSinZ * m_fSinX * m_fSinY + -1.f * m_fCosZ * m_fCosY;
				}

				if (m_pUp) {
					m_pUp->x = m_fCosZ * m_fSinX * m_fCosY + -m_fSinZ * -m_fSinY;
					m_pUp->y = m_fCosZ * m_fCosX;
					m_pUp->z = m_fCosZ * m_fSinX * m_fSinY + -m_fSinZ * m_fCosY;
				}
			}

			[[nodiscard]] auto Distance(Vector3& event) const -> float {
				const auto dx = this->x - event.x;
				const auto dy = this->y - event.y;
				const auto dz = this->z - event.z;
				return std::sqrt(dx * dx + dy * dy + dz * dz);
			}

			auto operator*(const float x) -> Vector3 {
				this->x *= x;
				this->y *= x;
				this->z *= x;
				return *this;
			}

			auto operator-(const float x) -> Vector3 {
				this->x -= x;
				this->y -= x;
				this->z -= x;
				return *this;
			}

			auto operator+(const float x) -> Vector3 {
				this->x += x;
				this->y += x;
				this->z += x;
				return *this;
			}

			auto operator/(const float x) -> Vector3 {
				this->x /= x;
				this->y /= x;
				this->z /= x;
				return *this;
			}

			auto operator*(const Vector3 x) -> Vector3 {
				this->x *= x.x;
				this->y *= x.y;
				this->z *= x.z;
				return *this;
			}

			auto operator-(const Vector3 x) -> Vector3 {
				this->x -= x.x;
				this->y -= x.y;
				this->z -= x.z;
				return *this;
			}

			auto operator+(const Vector3 x) -> Vector3 {
				this->x += x.x;
				this->y += x.y;
				this->z += x.z;
				return *this;
			}

			auto operator/(const Vector3 x) -> Vector3 {
				this->x /= x.x;
				this->y /= x.y;
				this->z /= x.z;
				return *this;
			}

			auto operator ==(const Vector3 x) const -> bool { return this->x == x.x && this->y == x.y && this->z == x.z; }
		};
#endif

#ifndef USE_GLM
		struct Vector2 {
			float x, y;

			Vector2() { x = y = 0.f; }

			Vector2(const float f1, const float f2) {
				x = f1;
				y = f2;
			}

			[[nodiscard]] auto Distance(const Vector2& event) const -> float {
				const auto dx = this->x - event.x;
				const auto dy = this->y - event.y;
				return std::sqrt(dx * dx + dy * dy);
			}

			auto operator*(const float x) -> Vector2 {
				this->x *= x;
				this->y *= x;
				return *this;
			}

			auto operator/(const float x) -> Vector2 {
				this->x /= x;
				this->y /= x;
				return *this;
			}

			auto operator+(const float x) -> Vector2 {
				this->x += x;
				this->y += x;
				return *this;
			}

			auto operator-(const float x) -> Vector2 {
				this->x -= x;
				this->y -= x;
				return *this;
			}

			auto operator*(const Vector2 x) -> Vector2 {
				this->x *= x.x;
				this->y *= x.y;
				return *this;
			}

			auto operator-(const Vector2 x) -> Vector2 {
				this->x -= x.x;
				this->y -= x.y;
				return *this;
			}

			auto operator+(const Vector2 x) -> Vector2 {
				this->x += x.x;
				this->y += x.y;
				return *this;
			}

			auto operator/(const Vector2 x) -> Vector2 {
				this->x /= x.x;
				this->y /= x.y;
				return *this;
			}

			auto operator ==(const Vector2 x) const -> bool { return this->x == x.x && this->y == x.y; }
		};
#endif

#ifndef USE_GLM
		struct Vector4 {
			float x, y, z, w;

			Vector4() { x = y = z = w = 0.F; }

			Vector4(const float f1, const float f2, const float f3, const float f4) {
				x = f1;
				y = f2;
				z = f3;
				w = f4;
			}

			auto operator*(const float x) -> Vector4 {
				this->x *= x;
				this->y *= x;
				this->z *= x;
				this->w *= x;
				return *this;
			}

			auto operator-(const float x) -> Vector4 {
				this->x -= x;
				this->y -= x;
				this->z -= x;
				this->w -= x;
				return *this;
			}

			auto operator+(const float x) -> Vector4 {
				this->x += x;
				this->y += x;
				this->z += x;
				this->w += x;
				return *this;
			}

			auto operator/(const float x) -> Vector4 {
				this->x /= x;
				this->y /= x;
				this->z /= x;
				this->w /= x;
				return *this;
			}

			auto operator*(const Vector4 x) -> Vector4 {
				this->x *= x.x;
				this->y *= x.y;
				this->z *= x.z;
				this->w *= x.w;
				return *this;
			}

			auto operator-(const Vector4 x) -> Vector4 {
				this->x -= x.x;
				this->y -= x.y;
				this->z -= x.z;
				this->w -= x.w;
				return *this;
			}

			auto operator+(const Vector4 x) -> Vector4 {
				this->x += x.x;
				this->y += x.y;
				this->z += x.z;
				this->w += x.w;
				return *this;
			}

			auto operator/(const Vector4 x) -> Vector4 {
				this->x /= x.x;
				this->y /= x.y;
				this->z /= x.z;
				this->w /= x.w;
				return *this;
			}

			auto operator ==(const Vector4 x) const -> bool { return this->x == x.x && this->y == x.y && this->z == x.z && this->w == x.w; }
		};
#endif

#ifndef USE_GLM
		struct Quaternion {
			float x, y, z, w;

			Quaternion() { x = y = z = w = 0.F; }

			Quaternion(const float f1, const float f2, const float f3, const float f4) {
				x = f1;
				y = f2;
				z = f3;
				w = f4;
			}

			auto Euler(float m_fX, float m_fY, float m_fZ) -> Quaternion {
				constexpr auto m_fDeg2Rad = std::numbers::pi_v<float> / 180.F;

				m_fX = m_fX * m_fDeg2Rad * 0.5F;
				m_fY = m_fY * m_fDeg2Rad * 0.5F;
				m_fZ = m_fZ * m_fDeg2Rad * 0.5F;

				const auto m_fSinX = sinf(m_fX);
				const auto m_fCosX = cosf(m_fX);

				const auto m_fSinY = sinf(m_fY);
				const auto m_fCosY = cosf(m_fY);

				const auto m_fSinZ = sinf(m_fZ);
				const auto m_fCosZ = cosf(m_fZ);

				x = m_fCosY * m_fSinX * m_fCosZ + m_fSinY * m_fCosX * m_fSinZ;
				y = m_fSinY * m_fCosX * m_fCosZ - m_fCosY * m_fSinX * m_fSinZ;
				z = m_fCosY * m_fCosX * m_fSinZ - m_fSinY * m_fSinX * m_fCosZ;
				w = m_fCosY * m_fCosX * m_fCosZ + m_fSinY * m_fSinX * m_fSinZ;

				return *this;
			}

			auto Euler(const Vector3& m_vRot) -> Quaternion { return Euler(m_vRot.x, m_vRot.y, m_vRot.z); }

			[[nodiscard]] auto ToEuler() const -> Vector3 {
				Vector3 m_vEuler;

				const auto m_fDist = (x * x) + (y * y) + (z * z) + (w * w);

				if (const auto m_fTest = x * w - y * z; m_fTest > 0.4995F * m_fDist) {
					m_vEuler.x = std::numbers::pi_v<float> *0.5F;
					m_vEuler.y = 2.F * atan2f(y, x);
					m_vEuler.z = 0.F;
				}
				else if (m_fTest < -0.4995F * m_fDist) {
					m_vEuler.x = std::numbers::pi_v<float> *-0.5F;
					m_vEuler.y = -2.F * atan2f(y, x);
					m_vEuler.z = 0.F;
				}
				else {
					m_vEuler.x = asinf(2.F * (w * x - y * z));
					m_vEuler.y = atan2f(2.F * w * y + 2.F * z * x, 1.F - 2.F * (x * x + y * y));
					m_vEuler.z = atan2f(2.F * w * z + 2.F * x * y, 1.F - 2.F * (z * z + x * x));
				}

				constexpr auto m_fRad2Deg = 180.F / std::numbers::pi_v<float>;
				m_vEuler.x *= m_fRad2Deg;
				m_vEuler.y *= m_fRad2Deg;
				m_vEuler.z *= m_fRad2Deg;

				return m_vEuler;
			}

			static auto LookRotation(const Vector3& forward) -> Quaternion {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Quaternion")->Get<Method>("LookRotation", { "UnityEngine.Vector3" });
				if (method) return method->Invoke<Quaternion, Vector3>(forward);
				return {};
			}

			auto operator*(const float x) -> Quaternion {
				this->x *= x;
				this->y *= x;
				this->z *= x;
				this->w *= x;
				return *this;
			}

			auto operator-(const float x) -> Quaternion {
				this->x -= x;
				this->y -= x;
				this->z -= x;
				this->w -= x;
				return *this;
			}

			auto operator+(const float x) -> Quaternion {
				this->x += x;
				this->y += x;
				this->z += x;
				this->w += x;
				return *this;
			}

			auto operator/(const float x) -> Quaternion {
				this->x /= x;
				this->y /= x;
				this->z /= x;
				this->w /= x;
				return *this;
			}

			auto operator*(const Quaternion x) -> Quaternion {
				this->x *= x.x;
				this->y *= x.y;
				this->z *= x.z;
				this->w *= x.w;
				return *this;
			}

			auto operator-(const Quaternion x) -> Quaternion {
				this->x -= x.x;
				this->y -= x.y;
				this->z -= x.z;
				this->w -= x.w;
				return *this;
			}

			auto operator+(const Quaternion x) -> Quaternion {
				this->x += x.x;
				this->y += x.y;
				this->z += x.z;
				this->w += x.w;
				return *this;
			}

			auto operator/(const Quaternion x) -> Quaternion {
				this->x /= x.x;
				this->y /= x.y;
				this->z /= x.z;
				this->w /= x.w;
				return *this;
			}

			auto operator ==(const Quaternion x) const -> bool { return this->x == x.x && this->y == x.y && this->z == x.z && this->w == x.w; }
		};
#endif

		struct Bounds {
			Vector3 m_vCenter;
			Vector3 m_vExtents;
		};

		struct Plane {
			Vector3 m_vNormal;
			float   fDistance;
		};

		struct Ray {
			Vector3 m_vOrigin;
			Vector3 m_vDirection;
		};

		struct RaycastHit {
			Vector3 m_Point;
			Vector3 m_Normal;
		};

		struct Rect {
			float fX, fY;
			float fWidth, fHeight;

			Rect() { fX = fY = fWidth = fHeight = 0.f; }

			Rect(const float f1, const float f2, const float f3, const float f4) {
				fX = f1;
				fY = f2;
				fWidth = f3;
				fHeight = f4;
			}
		};

		struct Color {
			float r, g, b, a;

			Color() { r = g = b = a = 0.f; }

			explicit Color(const float fRed = 0.f, const float fGreen = 0.f, const float fBlue = 0.f, const float fAlpha = 1.f) {
				r = fRed;
				g = fGreen;
				b = fBlue;
				a = fAlpha;
			}
		};

#ifndef USE_GLM
		struct Matrix4x4 {
			float m[4][4] = { {0} };

			Matrix4x4() = default;

			auto operator[](const int i) -> float* { return m[i]; }
		};
#endif

		struct Object {
			union {
				void* klass{ nullptr };
				void* vtable;
			}         Il2CppClass;

			struct MonitorData* monitor{ nullptr };

			auto GetType() -> CsType* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Object", "System")->Get<Method>("GetType");
				if (method) return method->Invoke<CsType*>(this);
				return nullptr;
			}

			auto ToString() -> String* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Object", "System")->Get<Method>("ToString");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			int GetHashCode() {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Object", "System")->Get<Method>("GetHashCode");
				if (method) return method->Invoke<int>(this);
				return 0;
			}
		};

		enum class BindingFlags : uint32_t {
			Default = 0,
			IgnoreCase = 1,
			DeclaredOnly = 2,
			Instance = 4,
			Static = 8,
			Public = 16,
			NonPublic = 32,
			FlattenHierarchy = 64,
			InvokeMethod = 256,
			CreateInstance = 512,
			GetField = 1024,
			SetField = 2048,
			GetProperty = 4096,
			SetProperty = 8192,
			PutDispProperty = 16384,
			PutRefDispProperty = 32768,
			ExactBinding = 65536,
			SuppressChangeType = 131072,
			OptionalParamBinding = 262144,
			IgnoreReturn = 16777216,
		};

		enum class FieldAttributes : uint32_t {
			FieldAccessMask = 7,
			PrivateScope = 0,
			Private = 1,
			FamANDAssem = 2,
			Assembly = 3,
			Family = 4,
			FamORAssem = 5,
			Public = 6,
			Static = 16,
			InitOnly = 32,
			Literal = 64,
			NotSerialized = 128,
			HasFieldRVA = 256,
			SpecialName = 512,
			RTSpecialName = 1024,
			HasFieldMarshal = 4096,
			PinvokeImpl = 8192,
			HasDefault = 32768,
			ReservedMask = 38144
		};

		enum class MemberTypes : uint32_t {
			Constructor = 1,
			Event = 2,
			Field = 4,
			Method = 8,
			Property = 16,
			TypeInfo = 32,
			Custom = 64,
			NestedType = 128,
			All = 191
		};

		struct MemberInfo {

		};

		struct FieldInfo : public MemberInfo {
			auto GetIsInitOnly() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsInitOnly");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsLiteral() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsLiteral");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsNotSerialized() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsNotSerialized");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsStatic() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsStatic");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsFamily() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsFamily");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsPrivate() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsPrivate");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsPublic() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_IsPublic");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetAttributes() -> FieldAttributes {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_Attributes");
				if (method) return method->Invoke<FieldAttributes>(this);
				return {};
			}

			auto GetMemberType() -> MemberTypes {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("get_MemberType");
				if (method) return method->Invoke<MemberTypes>(this);
				return {};
			}

			auto GetFieldOffset() -> int {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("GetFieldOffset");
				if (method) return method->Invoke<int>(this);
				return {};
			}

			template<typename T>
			auto GetValue(Object* object) -> T {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("GetValue");
				if (method) return method->Invoke<T>(this, object);
				return T();
			}

			template<typename T>
			auto SetValue(Object* object, T value) -> void {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("FieldInfo", "System.Reflection", "MemberInfo")->Get<Method>("SetValue", { "System.Object", "System.Object" });
				if (method) return method->Invoke<T>(this, object, value);
			}
		};

		struct CsType {
			auto FormatTypeName() -> String* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("FormatTypeName");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			auto GetFullName() -> String* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_FullName");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			auto GetNamespace() -> String* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_Namespace");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			auto GetIsSerializable() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsSerializable");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetContainsGenericParameters() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_ContainsGenericParameters");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsVisible() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsVisible");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsNested() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsNested");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsArray() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsArray");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsByRef() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsByRef");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsPointer() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsPointer");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsConstructedGenericType() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsConstructedGenericType");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsGenericParameter() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsGenericParameter");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsGenericMethodParameter() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsGenericMethodParameter");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsGenericType() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsGenericType");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsGenericTypeDefinition() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsGenericTypeDefinition");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsSZArray() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsSZArray");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsVariableBoundArray() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsVariableBoundArray");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetHasElementType() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_HasElementType");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsAbstract() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsAbstract");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsSealed() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsSealed");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsClass() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsClass");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsNestedAssembly() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsNestedAssembly");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsNestedPublic() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsNestedPublic");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsNotPublic() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsNotPublic");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsPublic() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsPublic");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsExplicitLayout() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsExplicitLayout");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsCOMObject() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsCOMObject");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsContextful() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsContextful");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsCollectible() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsCollectible");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsEnum() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsEnum");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsMarshalByRef() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsMarshalByRef");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsPrimitive() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsPrimitive");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsValueType() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsValueType");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsSignatureType() -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("get_IsSignatureType");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetField(const std::string& name, const BindingFlags flags = static_cast<BindingFlags>(static_cast<int>(BindingFlags::Instance) | static_cast<int>(BindingFlags::Static) | static_cast<int>(BindingFlags::Public))) -> FieldInfo* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Type", "System", "MemberInfo")->Get<Method>("GetField", { "System.String name", "System.Reflection.BindingFlags" });
				if (method) return method->Invoke<FieldInfo*>(this, String::New(name), flags);
				return nullptr;
			}
		};

		struct String : Object {
			int32_t  m_stringLength{ 0 };
			wchar_t m_firstChar[32]{};

			[[nodiscard]] auto ToString() const -> std::string {
				std::wstring_convert<std::codecvt_utf8<wchar_t>> converterX;
				return converterX.to_bytes(m_firstChar);
			}

			auto operator[](const int i) const -> wchar_t { return m_firstChar[i]; }

			auto operator=(const std::string& newString) const -> String* { return New(newString); }

			auto operator==(const std::wstring& newString) const -> bool { return Equals(newString); }

			auto Clear() -> void {

				memset(m_firstChar, 0, m_stringLength);
				m_stringLength = 0;
			}

			[[nodiscard]] auto Equals(const std::wstring& newString) const -> bool {

				if (newString.size() != m_stringLength) return false;
				if (std::memcmp(newString.data(), m_firstChar, m_stringLength) != 0) return false;
				return true;
			}

			static auto New(const std::string& str) -> String* {
				if (mode_ == Mode::Il2Cpp) return UnityResolve::Invoke<String*, const char*>("il2cpp_string_new", str.c_str());
				return UnityResolve::Invoke<String*, void*, const char*>("mono_string_new", UnityResolve::Invoke<void*>("mono_get_root_domain"), str.c_str());
			}
		};

		template <typename T>
		struct Array : Object {
			struct {
				std::uintptr_t length;
				std::int32_t   lower_bound;
			}*bounds{ nullptr };

			std::uintptr_t           max_length{ 0 };
			T** vector{};

			auto GetData() -> uintptr_t { return reinterpret_cast<uintptr_t>(&vector); }

			auto operator[](const unsigned int m_uIndex) -> T& { return *reinterpret_cast<T*>(GetData() + sizeof(T) * m_uIndex); }

			auto At(const unsigned int m_uIndex) -> T& { return operator[](m_uIndex); }

			auto Insert(T* m_pArray, uintptr_t m_uSize, const uintptr_t m_uIndex = 0) -> void {
				if ((m_uSize + m_uIndex) >= max_length) {
					if (m_uIndex >= max_length) return;

					m_uSize = max_length - m_uIndex;
				}

				for (uintptr_t u = 0; m_uSize > u; ++u) operator[](u + m_uIndex) = m_pArray[u];
			}

			auto Fill(T m_tValue) -> void { for (uintptr_t u = 0; max_length > u; ++u) operator[](u) = m_tValue; }

			auto RemoveAt(const unsigned int m_uIndex) -> void {
				if (m_uIndex >= max_length) return;

				if (max_length > (m_uIndex + 1)) for (auto u = m_uIndex; (max_length - m_uIndex) > u; ++u) operator[](u) = operator[](u + 1);

				--max_length;
			}

			auto RemoveRange(const unsigned int m_uIndex, unsigned int m_uCount) -> void {
				if (m_uCount == 0) m_uCount = 1;

				const auto m_uTotal = m_uIndex + m_uCount;
				if (m_uTotal >= max_length) return;

				if (max_length > (m_uTotal + 1)) for (auto u = m_uIndex; (max_length - m_uTotal) >= u; ++u) operator[](u) = operator[](u + m_uCount);

				max_length -= m_uCount;
			}

			auto RemoveAll() -> void {
				if (max_length > 0) {
					memset(GetData(), 0, sizeof(Type) * max_length);
					max_length = 0;
				}
			}

			auto ToVector() -> std::vector<T> {
				std::vector<T> rs{};
				rs.reserve(this->max_length);
				for (auto i = 0; i < this->max_length; i++) rs.push_back(this->At(i));
				return rs;
			}

			auto Resize(int newSize) -> void {
				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("Array")->Get<Method>("Resize");
				if (method) return method->Invoke<void>(this, newSize);
			}

			static auto New(const Class* kalss, const std::uintptr_t size) -> Array* {
				if (mode_ == Mode::Il2Cpp) return UnityResolve::Invoke<Array*, void*, std::uintptr_t>("il2cpp_array_new", kalss->address, size);
				return UnityResolve::Invoke<Array*, void*, void*, std::uintptr_t>("mono_array_new", pDomain, kalss->address, size);
			}
		};

		template <typename Type>
		struct List : Object {
			Array<Type>* pList;
			int          size{};
			int          version{};
			void* syncRoot{};

			auto ToArray() -> Array<Type>* { return pList; }

			static auto New(const Class* kalss, const std::uintptr_t size) -> List* {
				auto pList = new List<Type>();
				pList->pList = Array<Type>::New(kalss, size);
				pList->size = size;
				return pList;
			}

			auto operator[](const unsigned int m_uIndex) -> Type& { return pList->At(m_uIndex); }

			auto Add(Type pDate) -> void {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("Add");
				if (method) return method->Invoke<void>(this, pDate);
			}

			auto Remove(Type pDate) -> bool {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("Remove");
				if (method) return method->Invoke<bool>(this, pDate);
				return false;
			}

			auto RemoveAt(int index) -> void {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("RemoveAt");
				if (method) return method->Invoke<void>(this, index);
			}

			auto ForEach(void(*action)(Type pDate)) -> void {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("ForEach");
				if (method) return method->Invoke<void>(this, action);
			}

			auto GetRange(int index, int count) -> List* {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("GetRange");
				if (method) return method->Invoke<List*>(this, index, count);
				return nullptr;
			}

			auto Clear() -> void {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("Clear");
				if (method) return method->Invoke<void>(this);
			}

			auto Sort(int (*comparison)(Type* pX, Type* pY)) -> void {

				static Method* method;
				if (!method) method = Get("mscorlib.dll")->Get("List`1")->Get<Method>("Sort", { "*" });
				if (method) return method->Invoke<void>(this, comparison);
			}
		};

		template <typename TKey, typename TValue>
		struct Dictionary : Object {
			struct Entry {
				int    iHashCode;
				int    iNext;
				TKey   tKey;
				TValue tValue;
			};

			Array<int>* pBuckets;
			Array<Entry*>* pEntries;
			int            iCount;
			int            iVersion;
			int            iFreeList;
			int            iFreeCount;
			void* pComparer;
			void* pKeys;
			void* pValues;

			auto GetEntry() -> Entry* { return reinterpret_cast<Entry*>(pEntries->GetData()); }

			auto GetKeyByIndex(const int iIndex) -> TKey {
				TKey tKey = { 0 };

				Entry* pEntry = GetEntry();
				if (pEntry) tKey = pEntry[iIndex].tKey;

				return tKey;
			}

			auto GetValueByIndex(const int iIndex) -> TValue {
				TValue tValue = { 0 };

				Entry* pEntry = GetEntry();
				if (pEntry) tValue = pEntry[iIndex].tValue;

				return tValue;
			}

			auto GetValueByKey(const TKey tKey) -> TValue {
				TValue tValue = { 0 };
				for (auto i = 0; i < iCount; i++) if (GetEntry()[i].tKey == tKey) tValue = GetEntry()[i].tValue;
				return tValue;
			}

			auto operator[](const TKey tKey) const -> TValue { return GetValueByKey(tKey); }
		};

		struct UnityObject : Object {
			void* m_CachedPtr;

			auto GetName() -> String* {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Object")->Get<Method>("get_name");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			auto ToString() -> String* {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Object")->Get<Method>("ToString");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			static auto ToString(UnityObject* obj) -> String* {
				if (!obj) return {};
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Object")->Get<Method>("ToString", { "*" });
				if (method) return method->Invoke<String*>(obj);
				return {};
			}

			static auto Instantiate(UnityObject* original) -> UnityObject* {
				if (!original) return nullptr;
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Object")->Get<Method>("Instantiate", { "*" });
				if (method) return method->Invoke<UnityObject*>(original);
				return nullptr;
			}

			static auto Destroy(UnityObject* original) -> void {
				if (!original) return;
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Object")->Get<Method>("Destroy", { "*" });
				if (method) return method->Invoke<void>(original);
			}
		};

		struct Component : public UnityObject {
			auto GetTransform() -> Transform* {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("get_transform");
				if (method) return method->Invoke<Transform*>(this);
				return nullptr;
			}

			auto GetGameObject() -> GameObject* {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("get_gameObject");
				if (method) return method->Invoke<GameObject*>(this);
				return nullptr;
			}

			auto GetTag() -> String* {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("get_tag");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			template <typename T>
			auto GetComponentsInChildren() -> std::vector<T> {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponentsInChildren");
				if (method) return method->Invoke<Array<T>*>(this)->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponentsInChildren(Class* pClass) -> std::vector<T> {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponentsInChildren", { "System.Type" });
				if (method) return method->Invoke<Array<T>*>(this, pClass->GetType())->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponents() -> std::vector<T> {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponents");
				if (method) return method->Invoke<Array<T>*>(this)->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponents(Class* pClass) -> std::vector<T> {
				static Method* method;
				static void* obj;

				if (!method || !obj) {
					method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponents", { "System.Type" });
					obj = pClass->GetType();
				}
				if (method) return method->Invoke<Array<T>*>(this, obj)->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponentsInParent() -> std::vector<T> {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponentsInParent");
				if (method) return method->Invoke<Array<T>*>(this)->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponentsInParent(Class* pClass) -> std::vector<T> {
				static Method* method;
				static void* obj;

				if (!method || !obj) {
					method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponentsInParent", { "System.Type" });
					obj = pClass->GetType();
				}
				if (method) return method->Invoke<Array<T>*>(this, obj)->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponentInChildren(Class* pClass) -> T {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponentInChildren", { "System.Type" });
				if (method) return method->Invoke<T>(this, pClass->GetType());
				return T();
			}

			template <typename T>
			auto GetComponentInParent(Class* pClass) -> T {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Component")->Get<Method>("GetComponentInParent", { "System.Type" });
				if (method) return method->Invoke<T>(this, pClass->GetType());
				return T();
			}
		};

		struct Camera : Component {
			enum class Eye : int {
				Left,
				Right,
				Mono
			};

			static auto GetMain() -> Camera* {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("get_main");
				if (method) return method->Invoke<Camera*>();
				return nullptr;
			}

			static auto GetCurrent() -> Camera* {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("get_current");
				if (method) return method->Invoke<Camera*>();
				return nullptr;
			}

			static auto GetAllCount() -> int {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("get_allCamerasCount");
				if (method) return method->Invoke<int>();
				return 0;
			}

			static auto GetAllCamera() -> std::vector<Camera*> {
				static Method* method;
				static Class* klass;

				if (!method || !klass) {
					method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("GetAllCameras", { "*" });
					klass = Get("UnityEngine.CoreModule.dll")->Get("Camera");
				}

				if (method && klass) {
					if (const int count = GetAllCount(); count != 0) {
						const auto array = Array<Camera*>::New(klass, count);
						method->Invoke<int>(array);
						return array->ToVector();
					}
				}

				return {};
			}

			auto GetDepth() -> float {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("get_depth");
				if (method) return method->Invoke<float>(this);
				return 0.0f;
			}

			auto SetDepth(const float depth) -> void {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("set_depth", { "*" });
				if (method) return method->Invoke<void>(this, depth);
			}

			auto SetFoV(const float fov) -> void {

				static Method* method_fieldOfView;
				if (!method_fieldOfView) method_fieldOfView = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("set_fieldOfView", { "*" });
				if (method_fieldOfView) return method_fieldOfView->Invoke<void>(this, fov);
			}

			auto GetFoV() -> float {

				static Method* method_fieldOfView;
				if (!method_fieldOfView) method_fieldOfView = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("get_fieldOfView");
				if (method_fieldOfView) return method_fieldOfView->Invoke<float>(this);
				return 0.0f;
			}

			auto WorldToScreenPoint(const Vector3& position, const Eye eye = Eye::Mono) -> Vector3 {

				static Method* method;
				if (!method) {
					if (mode_ == Mode::Mono) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("WorldToScreenPoint_Injected");
					else method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>("WorldToScreenPoint", { "*", "*" });
				}
				if (mode_ == Mode::Mono && method) {
					const Vector3 vec3{};
					method->Invoke<void>(this, position, eye, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Vector3>(this, position, eye);
				return {};
			}

			auto ScreenToWorldPoint(const Vector3& position, const Eye eye = Eye::Mono) -> Vector3 {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>(mode_ == Mode::Mono ? "ScreenToWorldPoint_Injected" : "ScreenToWorldPoint");
				if (mode_ == Mode::Mono && method) {
					const Vector3 vec3{};
					method->Invoke<void>(this, position, eye, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Vector3>(this, position, eye);
				return {};
			}

			auto CameraToWorldMatrix() -> Matrix4x4 {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>(mode_ == Mode::Mono ? "get_cameraToWorldMatrix_Injected" : "get_cameraToWorldMatrix");
				if (mode_ == Mode::Mono && method) {
					Matrix4x4 matrix4{};
					method->Invoke<void>(this, &matrix4);
					return matrix4;
				}
				if (method) return method->Invoke<Matrix4x4>(this);
				return {};
			}


	                auto ScreenPointToRay(const Vector2 &position, const Eye eye = Eye::Mono) -> Ray {
	
	                        static Method *method;
	                	if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Camera")->Get<Method>(mode_ == Mode::Mono ? "ScreenPointToRay_Injected" : "ScreenPointToRay");
	                	if (mode_ == Mode::Mono && method) {
		                    	Ray ray{};
		                    	method->Invoke<void>(this, position, eye, &ray);
		                    	return ray;
	                	}
	                	if (method) return method->Invoke<Ray>(this, position, eye);
	                	return {};
	            	}
		};

		struct Transform : Component {
			auto GetPosition() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("get_position_Injected");
				const Vector3 vec3{};
				method->Invoke<void>(this, &vec3);
				return vec3;
			}

			auto SetPosition(const Vector3& position) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("set_position_Injected");
				return method->Invoke<void>(this, &position);
			}

			auto GetRight() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("get_right");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto SetRight(const Vector3& value) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("set_right");
				if (method) return method->Invoke<void>(this, value);
			}

			auto GetUp() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("get_up");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto SetUp(const Vector3& value) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("set_up");
				if (method) return method->Invoke<void>(this, value);
			}

			auto GetForward() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("get_forward");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto SetForward(const Vector3& value) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("set_forward");
				if (method) return method->Invoke<void>(this, value);
			}

			auto GetRotation() -> Quaternion {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "get_rotation_Injected" : "get_rotation");
				if (mode_ == Mode::Mono && method) {
					const Quaternion vec3{};
					method->Invoke<void>(this, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Quaternion>(this);
				return {};
			}

			auto SetRotation(const Quaternion& position) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "set_rotation_Injected" : "set_rotation");
				if (mode_ == Mode::Mono && method) return method->Invoke<void>(this, &position);
				if (method) return method->Invoke<void>(this, position);
			}

			auto GetLocalPosition() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "get_localPosition_Injected" : "get_localPosition");
				if (mode_ == Mode::Mono && method) {
					const Vector3 vec3{};
					method->Invoke<void>(this, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto SetLocalPosition(const Vector3& position) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "set_localPosition_Injected" : "set_localPosition");
				if (mode_ == Mode::Mono && method) return method->Invoke<void>(this, &position);
				if (method) return method->Invoke<void>(this, position);
			}

			auto GetLocalRotation() -> Quaternion {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "get_localRotation_Injected" : "get_localRotation");
				if (mode_ == Mode::Mono && method) {
					const Quaternion vec3{};
					method->Invoke<void>(this, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Quaternion>(this);
				return {};
			}

			auto SetLocalRotation(const Quaternion& position) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "set_localRotation_Injected" : "set_localRotation");
				if (mode_ == Mode::Mono && method) return method->Invoke<void>(this, &position);
				if (method) return method->Invoke<void>(this, position);
			}

			auto GetLocalScale() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("get_localScale_Injected");
				const Vector3 vec3{};
				method->Invoke<void>(this, &vec3);
				return vec3;
			}

			auto SetLocalScale(const Vector3& position) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "set_localScale_Injected" : "set_localScale");
				if (mode_ == Mode::Mono && method) return method->Invoke<void>(this, &position);
				if (method) return method->Invoke<void>(this, position);
			}

			auto GetChildCount() -> int {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("get_childCount");
				if (method) return method->Invoke<int>(this);
				return 0;
			}

			auto GetChild(const int index) -> Transform* {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("GetChild");
				if (method) return method->Invoke<Transform*>(this, index);
				return nullptr;
			}

			auto GetRoot() -> Transform* {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("GetRoot");
				if (method) return method->Invoke<Transform*>(this);
				return nullptr;
			}

			auto GetParent() -> Transform* {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("GetParent");
				if (method) return method->Invoke<Transform*>(this);
				return nullptr;
			}

			auto GetLossyScale() -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "get_lossyScale_Injected" : "get_lossyScale");
				if (mode_ == Mode::Mono && method) {
					const Vector3 vec3{};
					method->Invoke<void>(this, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto TransformPoint(const Vector3& position) -> Vector3 {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>(mode_ == Mode::Mono ? "TransformPoint_Injected" : "TransformPoint");
				if (mode_ == Mode::Mono && method) {
					const Vector3 vec3{};
					method->Invoke<void>(this, position, &vec3);
					return vec3;
				}
				if (method) return method->Invoke<Vector3>(this, position);
				return {};
			}

			auto LookAt(const Vector3& worldPosition) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("LookAt", { "Vector3" });
				if (method) return method->Invoke<void>(this, worldPosition);
			}

			auto Rotate(const Vector3& eulers) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Transform")->Get<Method>("Rotate", { "Vector3" });
				if (method) return method->Invoke<void>(this, eulers);
			}
		};

		struct GameObject : UnityObject {
			static auto Create(GameObject* obj, const std::string& name) -> void {
				if (!obj) return;
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("Internal_CreateGameObject");
				if (method) method->Invoke<void, GameObject*, String*>(obj, String::New(name));
			}

			static auto FindGameObjectsWithTag(const std::string& name) -> std::vector<GameObject*> {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("FindGameObjectsWithTag");
				if (method) {
					const auto array = method->Invoke<Array<GameObject*>*>(String::New(name));
					return array->ToVector();
				}
				return {};
			}

			static auto Find(const std::string& name) -> GameObject* {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("Find");
				if (method) return method->Invoke<GameObject*>(String::New(name));
				return nullptr;
			}

			auto GetActive() -> bool {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("get_active");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto SetActive(const bool value) -> void {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("set_active");
				if (method) return method->Invoke<void>(this, value);
			}

			auto GetActiveSelf() -> bool {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("get_activeSelf");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetActiveInHierarchy() -> bool {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("get_activeInHierarchy");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetIsStatic() -> bool {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("get_isStatic");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto GetTransform() -> Transform* {
				static Method* method;

				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("get_transform");
				if (method) return method->Invoke<Transform*>(this);
				return nullptr;
			}

			auto GetTag() -> String* {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("get_tag");
				if (method) return method->Invoke<String*>(this);
				return {};
			}

			template <typename T>
			auto GetComponent() -> T {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("GetComponent");
				if (method) return method->Invoke<T>(this);
				return T();
			}

			template <typename T>
			auto GetComponent(Class* type) -> T {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("GetComponent", { "System.Type" });
				if (method) return method->Invoke<T>(this, type->GetType());
				return T();
			}

			template <typename T>
			auto GetComponentInChildren(Class* type) -> T {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("GetComponentInChildren", { "System.Type" });
				if (method) return method->Invoke<T>(this, type->GetType());
				return T();
			}

			template <typename T>
			auto GetComponentInParent(Class* type) -> T {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("GetComponentInParent", { "System.Type" });
				if (method) return method->Invoke<T>(this, type->GetType());
				return T();
			}

			template <typename T>
			auto GetComponents(Class* type, bool useSearchTypeAsArrayReturnType = false, bool recursive = false, bool includeInactive = true, bool reverse = false, List<T>* resultList = nullptr) -> std::vector<T> {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("GameObject")->Get<Method>("GetComponentsInternal");
				if (method) return method->Invoke<Array<T>*>(this, type->GetType(), useSearchTypeAsArrayReturnType, recursive, includeInactive, reverse, resultList)->ToVector();
				return {};
			}

			template <typename T>
			auto GetComponentsInChildren(Class* type, const bool includeInactive = false) -> std::vector<T> { return GetComponents<T>(type, false, true, includeInactive, false, nullptr); }


			template <typename T>
			auto GetComponentsInParent(Class* type, const bool includeInactive = false) -> std::vector<T> { return GetComponents<T>(type, false, true, includeInactive, true, nullptr); }
		};

		struct LayerMask : Object {
			int m_Mask;

			static auto NameToLayer(const std::string& layerName) -> int {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("LayerMask")->Get<Method>("NameToLayer");
				if (method) return method->Invoke<int>(String::New(layerName));
				return 0;
			}

			static auto LayerToName(const int layer) -> String* {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("LayerMask")->Get<Method>("LayerToName");
				if (method) return method->Invoke<String*>(layer);
				return {};
			}
		};

		struct Rigidbody : Component {
			auto GetDetectCollisions() -> bool {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Rigidbody")->Get<Method>("get_detectCollisions");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto SetDetectCollisions(const bool value) -> void {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Rigidbody")->Get<Method>("set_detectCollisions");
				if (method) return method->Invoke<void>(this, value);
			}

			auto GetVelocity() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Rigidbody")->Get<Method>(mode_ == Mode::Mono ? "get_velocity_Injected" : "get_velocity");
				if (mode_ == Mode::Mono && method) {
					Vector3 vector;
					method->Invoke<void>(this, &vector);
					return vector;
				}
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto SetVelocity(Vector3 value) -> void {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Rigidbody")->Get<Method>(mode_ == Mode::Mono ? "set_velocity_Injected" : "set_velocity");
				if (mode_ == Mode::Mono && method) return method->Invoke<void>(this, &value);
				if (method) return method->Invoke<void>(this, value);
			}
		};

		struct Collider : Component {
			auto GetBounds() -> Bounds {

				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Collider")->Get<Method>("get_bounds_Injected");
				if (method) {
					Bounds bounds;
					method->Invoke<void>(this, &bounds);
					return bounds;
				}
				return {};
			}
		};

		struct Mesh : UnityObject {
			auto GetBounds() -> Bounds {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Mesh")->Get<Method>("get_bounds_Injected");
				if (method) {
					Bounds bounds;
					method->Invoke<void>(this, &bounds);
					return bounds;
				}
				return {};
			}
		};

		struct CapsuleCollider : Collider {
			auto GetCenter() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("CapsuleCollider")->Get<Method>("get_center");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto GetDirection() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("CapsuleCollider")->Get<Method>("get_direction");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto GetHeightn() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("CapsuleCollider")->Get<Method>("get_height");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto GetRadius() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("CapsuleCollider")->Get<Method>("get_radius");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}
		};

		struct BoxCollider : Collider {
			auto GetCenter() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("BoxCollider")->Get<Method>("get_center");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}

			auto GetSize() -> Vector3 {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("BoxCollider")->Get<Method>("get_size");
				if (method) return method->Invoke<Vector3>(this);
				return {};
			}
		};

		struct Renderer : Component {
			auto GetBounds() -> Bounds {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Renderer")->Get<Method>("get_bounds_Injected");
				if (method) {
					Bounds bounds;
					method->Invoke<void>(this, &bounds);
					return bounds;
				}
				return {};
			}
		};

		struct Behaviour : public Component {
			auto GetEnabled() -> bool {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Behaviour")->Get<Method>("get_enabled");
				if (method) return method->Invoke<bool>(this);
				return false;
			}

			auto SetEnabled(const bool value) -> void {

				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Behaviour")->Get<Method>("set_enabled");
				if (method) return method->Invoke<void>(this, value);
			}
		};

		struct MonoBehaviour : public Behaviour {};

		struct Physics : Object {
			static auto Linecast(const Vector3& start, const Vector3& end) -> bool {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Physics")->Get<Method>("Linecast", { "*", "*" });
				if (method) return method->Invoke<bool>(start, end);
				return false;
			}

			static auto Raycast(const Vector3& origin, const Vector3& direction, const float maxDistance) -> bool {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Physics")->Get<Method>("Raycast", { "UnityEngine.Vector3", "UnityEngine.Vector3", "System.Single" });
				if (method) return method->Invoke<bool>(origin, direction, maxDistance);
				return false;
			}

			static auto Raycast(const Ray& origin, const RaycastHit* direction, const float maxDistance) -> bool {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Physics")->Get<Method>("Raycast", { "UnityEngine.Ray", "UnityEngine.RaycastHit&", "System.Single" });
				if (method) return method->Invoke<bool, Ray>(origin, direction, maxDistance);
				return false;
			}

			static auto IgnoreCollision(Collider* collider1, Collider* collider2) -> void {
				static Method* method;
				if (!method) method = Get("UnityEngine.PhysicsModule.dll")->Get("Physics")->Get<Method>("IgnoreCollision1", { "*", "*" });
				if (method) return method->Invoke<void>(collider1, collider2);
			}
		};

		struct Animator : Behaviour {
			enum class HumanBodyBones : int {
				Hips,
				LeftUpperLeg,
				RightUpperLeg,
				LeftLowerLeg,
				RightLowerLeg,
				LeftFoot,
				RightFoot,
				Spine,
				Chest,
				UpperChest = 54,
				Neck = 9,
				Head,
				LeftShoulder,
				RightShoulder,
				LeftUpperArm,
				RightUpperArm,
				LeftLowerArm,
				RightLowerArm,
				LeftHand,
				RightHand,
				LeftToes,
				RightToes,
				LeftEye,
				RightEye,
				Jaw,
				LeftThumbProximal,
				LeftThumbIntermediate,
				LeftThumbDistal,
				LeftIndexProximal,
				LeftIndexIntermediate,
				LeftIndexDistal,
				LeftMiddleProximal,
				LeftMiddleIntermediate,
				LeftMiddleDistal,
				LeftRingProximal,
				LeftRingIntermediate,
				LeftRingDistal,
				LeftLittleProximal,
				LeftLittleIntermediate,
				LeftLittleDistal,
				RightThumbProximal,
				RightThumbIntermediate,
				RightThumbDistal,
				RightIndexProximal,
				RightIndexIntermediate,
				RightIndexDistal,
				RightMiddleProximal,
				RightMiddleIntermediate,
				RightMiddleDistal,
				RightRingProximal,
				RightRingIntermediate,
				RightRingDistal,
				RightLittleProximal,
				RightLittleIntermediate,
				RightLittleDistal,
				LastBone = 55
			};

			auto GetBoneTransform(const HumanBodyBones humanBoneId) -> Transform* {
				static Method* method;

				if (!method) method = Get("UnityEngine.AnimationModule.dll")->Get("Animator")->Get<Method>("GetBoneTransform");
				if (method) return method->Invoke<Transform*>(this, humanBoneId);
				return nullptr;
			}
		};

		struct Time {
			static auto GetTime() -> float {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Time")->Get<Method>("get_time");
				if (method) return method->Invoke<float>();
				return 0.0f;
			}

			static auto GetDeltaTime() -> float {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Time")->Get<Method>("get_deltaTime");
				if (method) return method->Invoke<float>();
				return 0.0f;
			}

			static auto GetFixedDeltaTime() -> float {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Time")->Get<Method>("get_fixedDeltaTime");
				if (method) return method->Invoke<float>();
				return 0.0f;
			}

			static auto GetTimeScale() -> float {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Time")->Get<Method>("get_timeScale");
				if (method) return method->Invoke<float>();
				return 0.0f;
			}

			static auto SetTimeScale(const float value) -> void {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Time")->Get<Method>("set_timeScale");
				if (method) return method->Invoke<void>(value);
			}
		};

		struct Screen {
			static auto get_width() -> Int32 {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Screen")->Get<Method>("get_width");
				if (method) return method->Invoke<int32_t>();
				return 0;
			}

			static auto get_height() -> Int32 {
				static Method* method;
				if (!method) method = Get("UnityEngine.CoreModule.dll")->Get("Screen")->Get<Method>("get_height");
				if (method) return method->Invoke<int32_t>();
				return 0;
			}
		};

		template <typename Return, typename... Args>
		static auto Invoke(void* address, Args... args) -> Return {
#if WINDOWS_MODE
			if (address != nullptr) return reinterpret_cast<Return(*)(Args...)>(address)(args...);
#elif LINUX_MODE || ANDROID_MODE || IOS_MODE || HARMONYOS_MODE
			if (address != nullptr) return ((Return(*)(Args...))(address))(args...);
#endif
			return Return();
		}
	};

private:
	inline static Mode                                   mode_{};
	inline static void* hmodule_;
	inline static std::unordered_map<std::string, void*> address_{};
	inline static void* pDomain{};
};
#endif // UNITYRESOLVE_HPPs
