#ifndef UNITYRESOLVE_HPP
#define UNITYRESOLVE_HPP
#undef GetObject
#include <map>
#include <format>
#include <fstream>
#include <string>
#include <mutex>
#include <unordered_map>
#include <iostream>
#include <windows.h>
#include <vector>

class UnityResolve final {
public:
	struct Assembly;
	struct Type;
	struct Class;
	struct Field;
	struct Method;

	enum class Mode : char {
		Il2cpp,
		Mono,
		Auto
	};

	struct Assembly final {
		void*                         address;
		std::string                   name;
		std::string                   file;
		std::unordered_map<std::string, Class*> classes;
	};

	struct Type final {
		void*       address;
		std::string name;
		int         size;

		[[nodiscard]] auto GetObject() const -> void* {
			if (mode_ == Mode::Il2cpp) {
				return Invoke<void*>("il2cpp_type_get_object", address);
			}
			return Invoke<void*>("mono_type_get_object", pDomain, address);
		}
	};

	struct Class final {
		void*						   classinfo;
		std::string                    name;
		std::string                    parent;
		std::string                    namespaze;
		std::map<std::string, Field*>  fields;
		std::unordered_map<std::string, Method*> methods;

		template<typename RType>
		auto Get(const std::string& name) -> RType* {
			if (std::is_same_v<RType, Field> && fields.contains(name))
				return static_cast<RType*>(fields[name]);
			if (std::is_same_v<RType, Method> && methods.contains(name))
				return static_cast<RType*>(methods[name]);
			return nullptr;
		}

		[[nodiscard]] auto GetType() const -> Type {
			if (mode_ == Mode::Il2cpp) {
				void* pUType = Invoke<void*, void*>("il2cpp_class_get_type", classinfo);
				return { pUType, name, -1 };
			}
			void* pUType = Invoke<void*, void*>("mono_class_get_type", classinfo);
			return { pUType, name, -1 };
		}

		/**
		 * \brief 获取类所有实例
		 * \tparam T 返回数组类型
		 * \param type 类
		 * \return 返回实例指针数组
		 */
		template<typename T>
		auto FindObjectsByType() -> std::vector<T> {
			static Method* pMethod;

			if (!pMethod)
				pMethod = assembly["UnityEngine.CoreModule"]->classes["Object"]->methods[mode_ == Mode::Il2cpp ? "FindObjectsOfType" : "FindObjectsOfTypeAll"];
			
			if (pMethod) {
				std::vector<T> rs{};
				auto array = pMethod->Invoke<UnityType::Array<T>*>(this->GetType().GetObject());
				rs.reserve(array->max_length);
				for (int i = 0; i < array->max_length; i++)
					rs.push_back(array->At(i));
				return rs;
			}

			throw std::logic_error("FindObjectsOfType nullptr");
		}
	};

	struct Field final {
		void*        fieldinfo;
		std::string  name;
		Type*        type;
		Class*       klass;
		std::int32_t offset; // If offset is -1, then it's thread static
		bool         static_field;
		void*        vTable;

		template<typename T>
		auto SetValue(T* value) const -> void {
			if (!static_field)   
				return;

			if (mode_ == Mode::Il2cpp) {
				return Invoke<void, void*, T*>("il2cpp_field_static_set_value", fieldinfo, value);
			}
			if (vTable) {
				return Invoke<void, void*, void*, T*>("mono_field_static_set_value", vTable, fieldinfo, value);
			}
		}

		template<typename T>
		auto GetValue(T* value) const -> void {
			if (!static_field)
				return;

			if (mode_ == Mode::Il2cpp) {
				return Invoke<void, void*, T*>("il2cpp_field_static_get_value", fieldinfo, value);
			}
			if (vTable) {
				return Invoke<void, void*, void*, T*>("mono_field_static_get_value", vTable, fieldinfo, value);
			}
		}
	};

	struct Method final {
		void*        address;
		std::string  name;
		Class*       klass;
		Type*        return_type;
		std::int32_t flags;
		bool         static_function;
		void*        function;

		std::map<std::string, const Type*> args;

		template<typename Return, typename... Args>
		auto Invoke(Args... args) -> Return {
			Compile();
			if (function)
				return static_cast<Return(*)(Args...)>(function)(args...);
			throw std::logic_error("nullptr");
		}

		auto Compile() -> void {
			if (address && !function && mode_ == Mode::Mono)
				function = UnityResolve::Invoke<void*>("mono_compile_method", address);
		}

		template<typename Return>
		auto RuntimeInvoke(void* obj, void** args) -> Return* {
			if (mode_ == Mode::Il2cpp) {
				return static_cast<Return*>(UnityResolve::Invoke<void*>(
					"il2cpp_runtime_invoke",
					address,
					obj,
					args,
					nullptr));
			}
			return static_cast<Return*>(UnityResolve::Invoke<void
				*>("mono_runtime_invoke", address, obj, args, nullptr));
		}

		template<typename Return, typename... Args>
		using MethodPointer = Return(*)(Args...);

		template<typename Return, typename... Args>
		auto Cast() -> MethodPointer<Return, Args...> {
			Compile();
			if (function)
				return static_cast<MethodPointer<Return, Args...>>(function);
			throw std::logic_error("nullptr");
		}

		static auto FindMethod(const std::string& klass,
							   const std::string& name,
							   const std::string& namespaze,
							   const std::string& assembly_name,
							   const size_t       args) -> Method* {
			const auto vklass = assembly[assembly_name]->classes[klass];
			if (const auto vmethod = vklass->namespaze == namespaze ? vklass->methods[name] : nullptr; vmethod &&
				vmethod->args.size() == args)
				return vmethod;
			return nullptr;
		}
	};

	static auto ThreadAttach() -> void {
		if (mode_ == Mode::Il2cpp) {
			Invoke<void*>("il2cpp_thread_attach", pDomain);
		}
		else {
			Invoke<void*>("mono_thread_attach", pDomain);
			Invoke<void*>("mono_jit_thread_attach", pDomain);
		}
		
	}

	static auto Init(const HMODULE hmodule, const Mode mode = Mode::Auto) -> void {
		mode_    = mode;
		hmodule_ = hmodule;

		if (mode == Mode::Auto) {
			char path[0xFF];
			GetModuleFileNameA(hmodule, path, 0xFF);
			const std::string file{path};
			auto              ret = file.substr(0, file.find_first_of('\\') + 1);
			if (file.find("mono") != std::string::npos)
				mode_ = Mode::Mono;
			else
				mode_ = Mode::Il2cpp;
		}

		if (mode_ == Mode::Il2cpp) {
			pDomain = Invoke<void*>("il2cpp_domain_get");
			Invoke<void*>("il2cpp_thread_attach", pDomain);

			size_t     nrofassemblies = 0;
			const auto assemblies     = Invoke<void**>("il2cpp_domain_get_assemblies", pDomain, &nrofassemblies);
			for (auto i = 0; i < nrofassemblies; i++) {
				const auto ptr = assemblies[i];
				if (ptr == nullptr)
					return;

				const auto assembly = new Assembly{
					.address = ptr, .name = Invoke<const char*>("il2cpp_class_get_assemblyname", ptr)
				};
				UnityResolve::assembly[assembly->name] = assembly;

				const void* image = Invoke<void*>("il2cpp_assembly_get_image", ptr);
				assembly->file    = Invoke<const char*>("il2cpp_image_get_filename", image);
				const int count   = Invoke<int>("il2cpp_image_get_class_count", image);

				int iClass{ 0 };
				for (int i = 0; i < count; i++) {
					const auto pClass = Invoke<void*>("il2cpp_image_get_class", image, i);
					if (pClass == nullptr)
						continue;

					const auto pAClass = new Class();
					pAClass->classinfo = pClass;
					pAClass->name      = Invoke<const char*>("il2cpp_class_get_name", pClass);
					if (const auto pPClass = Invoke<void*>("il2cpp_class_get_parent", pClass))
						pAClass->parent = Invoke<const char*>("il2cpp_class_get_name", pPClass);

					pAClass->namespaze               = Invoke<const char*>("il2cpp_class_get_namespace", pClass);
					if (!assembly->classes.contains(pAClass->name)) {
						assembly->classes[pAClass->name] = pAClass;
					}
					else {
						iClass++;
						assembly->classes[pAClass->name + std::to_string(iClass)] = pAClass;
					}

					void* iter = nullptr;
					void* field;
					do {
						if ((field = Invoke<void*>("il2cpp_class_get_fields", pClass, &iter))) {
							const auto pField = new Field{
								.fieldinfo = field, .name = Invoke<const char*>("il2cpp_field_get_name", field),
								.type = new Type{.address = Invoke<void*>("il2cpp_field_get_type", field)},
								.klass = pAClass, .offset = Invoke<int>("il2cpp_field_get_offset", field),
								.static_field = false,
								.vTable = nullptr
							};
							int tSize{};
							pField->static_field = pField->offset == -1;
							pField->type->name = Invoke<const char*>("il2cpp_type_get_name", pField->type->address);
							pField->type->size = -1;
							pAClass->fields[pField->name] = pField;
						}
					}
					while (field);
					iter = nullptr;

					int iMethod{0};
					do {
						if ((field = Invoke<void*>("il2cpp_class_get_methods", pClass, &iter))) {
							int        fFlags{};
							const auto pMethod = new Method{
								.address = field, .name = Invoke<const char*>("il2cpp_method_get_name", field),
								.klass = pAClass,
								.return_type = new Type{
									.address = Invoke<void*>("il2cpp_method_get_return_type", field),
								},
								.flags = Invoke<int>("il2cpp_method_get_flags", field, &fFlags)
							};
							int tSize{};
							pMethod->static_function   = pMethod->flags & 0x10;
							pMethod->return_type->name = Invoke<const char*>("il2cpp_type_get_name", pMethod->return_type->address);
							pMethod->return_type->size      = -1;
							pMethod->function               = *static_cast<void**>(field);
							if (!pAClass->methods.contains(pMethod->name)) {
								pAClass->methods[pMethod->name] = pMethod; 
							}
							else {
								iMethod++;
								pAClass->methods[pMethod->name + std::to_string(iMethod)] = pMethod;
							}

							const auto argCount = Invoke<int>("il2cpp_method_get_param_count", field);

							for (int index = 0; index < argCount; index++) {
								pMethod->args[Invoke<const char*>("il2cpp_method_get_param_name", field, index)] = new
									Type{
										.address = Invoke<void*>("il2cpp_method_get_param", field, index),
										.name = Invoke<const char*>("il2cpp_type_get_name", Invoke<void*>("il2cpp_method_get_param", field, index)),
										.size = -1
									};
							}
						}
					}
					while (field);
					iter = nullptr;
					iMethod = 0;
					const void* i_class{};
					const void* iiter{};

					do {
						if ((i_class = Invoke<void*>("il2cpp_class_get_interfaces", pClass, &iiter))) {
							do {
								if ((field = Invoke<void*>("il2cpp_class_get_fields", i_class, &iter))) {
									const auto pField = new Field{
										.fieldinfo = field, .name = Invoke<const char*>("il2cpp_field_get_name", field),
										.type = new Type{.address = Invoke<void*>("il2cpp_field_get_type", field)},
										.klass = pAClass, .offset = Invoke<int>("il2cpp_field_get_offset", field),
										.static_field = false,
										.vTable = nullptr
									};
									int tSize{};
									pField->static_field = pField->offset == -1;
									pField->type->name = Invoke<const char*>("il2cpp_type_get_name", pField->type->address);
									pField->type->size = -1;
									pAClass->fields[pField->name] = pField;
								}
							} while (field);
							iter = nullptr;

							do {
								if ((field = Invoke<void*>("il2cpp_class_get_methods", i_class, &iter))) {
									int        fFlags{};
									const auto pMethod = new Method{
										.address = field, .name = Invoke<const char*>("il2cpp_method_get_name", field),
										.klass = pAClass,
										.return_type = new Type{
											.address = Invoke<void*>("il2cpp_method_get_return_type", field),
										},
										.flags = Invoke<int>("il2cpp_method_get_flags", field, &fFlags)
									};
									int tSize{};
									pMethod->static_function = pMethod->flags & 0x10;
									pMethod->return_type->name = Invoke<const char*>("il2cpp_type_get_name", pMethod->return_type->address);
									pMethod->return_type->size = -1;
									pMethod->function = *static_cast<void**>(field);
									if (!pAClass->methods.contains(pMethod->name)) {
										pAClass->methods[pMethod->name] = pMethod;
									}
									else {
										iMethod++;
										pAClass->methods[pMethod->name + std::to_string(iMethod)] = pMethod;
									}

									const auto argCount = Invoke<int>("il2cpp_method_get_param_count", field);

									for (int index = 0; index < argCount; index++) {
										pMethod->args[Invoke<const char*>("il2cpp_method_get_param_name", field, index)] = new
											Type{
												.address = Invoke<void*>("il2cpp_method_get_param", field, index),
												.name = Invoke<const char*>("il2cpp_type_get_name", Invoke<void*>("il2cpp_method_get_param", field, index)),
												.size = -1
										};
									}
								}
							} while (field);
							iter = nullptr;
							iMethod = 0;
						}
					} while (i_class);
				}
			}
		}
		else {
			pDomain = Invoke<void*>("mono_get_root_domain");
			Invoke<void*>("mono_thread_attach", pDomain);
			Invoke<void*>("mono_jit_thread_attach", pDomain);

			Invoke<void*, void(*)(void* ptr, std::unordered_map<std::string, Assembly*>&), std::unordered_map<std::string, Assembly*>&>("mono_assembly_foreach",
													   [](void* ptr, std::unordered_map<std::string, Assembly*>& v) {
														   if (ptr == nullptr)
															   return;

														   const auto assembly = new Assembly{
															   .address = ptr,
														   };

														   const void* image = Invoke<void*>("mono_assembly_get_image", ptr);
														   assembly->file = Invoke<const char*>("mono_image_get_filename", image);
														   assembly->name = Invoke<const char*>("mono_image_get_name", image);

														   const void* table = Invoke<void*>("mono_image_get_table_info", image, 2);
														   const int count = Invoke<int>("mono_table_info_get_rows", table);
														   v[assembly->name] = assembly;

														   int iClass{};
														   for (int i = 0; i < count; i++) {
															   const auto pClass = Invoke<void*>("mono_class_get", image, 0x02000000 | (i + 1));
															   if (pClass == nullptr)
																   continue;

															   const auto pAClass = new Class();
															   pAClass->classinfo = pClass;
															   pAClass->name      = Invoke<const char*>("mono_class_get_name", pClass);
															   if (const auto pPClass = Invoke<void*>("mono_class_get_parent", pClass)) {
																   pAClass->parent = Invoke<const char*>("mono_class_get_name", pPClass);
															   }
															   pAClass->namespaze = Invoke<const char*>("mono_class_get_namespace", pClass);
															   if (!assembly->classes.contains(pAClass->name)) {
																   assembly->classes[pAClass->name] = pAClass;
															   }
															   else {
																   iClass++;
																   assembly->classes[pAClass->name + std::to_string(iClass)] = pAClass;
															   }

															   void* iter = nullptr;
															   void* field;
															   do {
																   if ((field = Invoke<void*>("mono_class_get_fields", pClass, &iter))) {
																	   const auto pField = new Field{
																		   .fieldinfo = field,
																		   .name = Invoke<const char*>("mono_field_get_name", field),
																		   .type = new Type{
																			   .address = Invoke<void*>("mono_field_get_type", field)
																		   },
																		   .klass = pAClass,
																		   .offset = Invoke<int>("mono_field_get_offset", field),
																		   .static_field = false,
																		   .vTable = nullptr
																	   };
																	   int tSize{};
																	   pField->static_field = pField->offset == -1;
																	   pField->type->name   = Invoke<const char*>("mono_type_get_name", pField->type->address);
																	   pField->type->size = Invoke<int>("mono_type_size",pField->type->address, &tSize);
																	   pAClass->fields[pField->name] = pField;
																	   if (pField->static_field)
																		   pField->vTable = Invoke<void*>("mono_class_vtable", pDomain, pClass);
																   }
															   }
															   while (field);
															   iter = nullptr;

															   int iMethod{ 0 };
															   do {
																   if ((field = Invoke<void*>("mono_class_get_methods", pClass, &iter))) {
																	   const auto signature = Invoke<void*>("mono_method_signature", field);
																	   int        fFlags{};
																	   const auto pMethod = new Method{
																		   .address = field,
																		   .name = Invoke<const char*>("mono_method_get_name", field),
																		   .klass = pAClass,
																		   .return_type = new Type{
																			   .address = Invoke<void*>("mono_signature_get_return_type", signature),
																		   },
																		   .flags = Invoke<int>("mono_method_get_flags", field, &fFlags)
																	   };
																	   int tSize{};
																	   pMethod->static_function = pMethod->flags & 0x10;
																	   pMethod->return_type->name = Invoke<const char*>("mono_type_get_name", pMethod->return_type->address);
																	   pMethod->return_type->size = Invoke<int>("mono_type_size", pMethod->return_type->address, &tSize);
																	   if (!pAClass->methods.contains(pMethod->name)) {
																		   pAClass->methods[pMethod->name] = pMethod;
																	   }
																	   else {
																		   iMethod++;
																		   pAClass->methods[pMethod->name + std::to_string(iMethod)] = pMethod;
																	   }

																	   const auto names = new char*[Invoke<int>("mono_signature_get_param_count", signature)];
																	   Invoke<void>("mono_method_get_param_names", field, names);

																	   void* mIter = nullptr;
																	   void* mType;
																	   int   iname = 0;
																	   do {
																		   if ((mType = Invoke<void*>("mono_signature_get_params", signature, &mIter))) {
																			   int t_size{};
																			   pMethod->args[names[iname]] = new Type{
																				   .address = mType,
																				   .name = Invoke<const char*>("mono_type_get_name", mType),
																				   .size = Invoke<int>( "mono_type_size", mType, &t_size)
																			   };
																			   iname++;
																		   }
																	   }
																	   while (mType);
																   }
															   }
															   while (field);
															   iter = nullptr;
															   iMethod = 0;
															   const void* iClass{};
															   const void* iiter{};

															   do {
																   if ((iClass = Invoke<void*>("mono_class_get_interfaces", pClass, &iiter))) {
																	   do {
																		   if ((field = Invoke<void*>("mono_class_get_fields", iClass, &iter))) {
																			   const auto pField = new Field{
																				   .fieldinfo = field,
																				   .name = Invoke<const char*>("mono_field_get_name", field),
																				   .type = new Type{
																					   .address = Invoke<void*>("mono_field_get_type", field)
																				   },
																				   .klass = pAClass,
																				   .offset = Invoke<int>("mono_field_get_offset", field),
																				   .static_field = false,
																				   .vTable = nullptr
																			   };
																			   int tSize{};
																			   pField->static_field = pField->offset == -1;
																			   pField->type->name = Invoke<const char*>("mono_type_get_name", pField->type->address);
																			   pField->type->size = Invoke<int>("mono_type_size", pField->type->address, &tSize);
																			   pAClass->fields[pField->name] = pField;
																			   if (pField->static_field)
																				   pField->vTable = Invoke<void*>("mono_class_vtable", pDomain, pClass);
																		   }
																	   } while (field);
																	   iter = nullptr;

																	   do {
																		   if ((field = Invoke<void*>("mono_class_get_methods", iClass, &iter))) {
																			   const auto signature = Invoke<void*>("mono_method_signature", field);
																			   int        fFlags{};
																			   const auto pMethod = new Method{
																				   .address = field,
																				   .name = Invoke<const char*>("mono_method_get_name", field),
																				   .klass = pAClass,
																				   .return_type = new Type{
																					   .address = Invoke<void*>("mono_signature_get_return_type", signature),
																				   },
																				   .flags = Invoke<int>("mono_method_get_flags", field, &fFlags)
																			   };
																			   int tSize{};
																			   pMethod->static_function = pMethod->flags & 0x10;
																			   pMethod->return_type->name = Invoke<const char*>("mono_type_get_name", pMethod->return_type->address);
																			   pMethod->return_type->size = Invoke<int>("mono_type_size", pMethod->return_type->address, &tSize);
																			   if (!pAClass->methods.contains(pMethod->name)) {
																				   pAClass->methods[pMethod->name] = pMethod;
																			   }
																			   else {
																				   iMethod++;
																				   pAClass->methods[pMethod->name + std::to_string(iMethod)] = pMethod;
																			   }

																			   const auto names = new char* [Invoke<int>("mono_signature_get_param_count", signature)];
																			   Invoke<void>("mono_method_get_param_names", field, names);

																			   void* mIter = nullptr;
																			   void* mType;
																			   int   iname = 0;
																			   do {
																				   if ((mType = Invoke<void*>("mono_signature_get_params", signature, &mIter))) {
																					   int t_size{};
																					   pMethod->args[names[iname]] = new Type{
																						   .address = mType,
																						   .name = Invoke<const char*>("mono_type_get_name", mType),
																						   .size = Invoke<int>("mono_type_size", mType, &t_size)
																					   };
																					   iname++;
																				   }
																			   } while (mType);
																		   }
																	   } while (field);
																	   iter = nullptr;
																	   iMethod = 0;
																   }
															   } while (iClass);
														   }
													   },
													   assembly);
		}
	}

	static auto DumpToFile(const std::string& file) -> void {
		std::ofstream io(file, std::fstream::out);

		if (!io)
			return;

		io << "/*" << "\n" <<
			R"(*  __  __                      __                  ____                                ___                       )" << "\n" <<
			R"(* /\ \/\ \              __    /\ \__              /\  _`\                             /\_ \                      )"<< "\n" <<
			R"(* \ \ \ \ \     ___    /\_\   \ \ ,_\   __  __    \ \ \L\ \      __     ____    ___   \//\ \     __  __     __   )"<< "\n" <<
			R"(*  \ \ \ \ \  /' _ `\  \/\ \   \ \ \/  /\ \/\ \    \ \ ,  /    /'__`\  /',__\  / __`\   \ \ \   /\ \/\ \  /'__`\ )"<< "\n" <<
			R"(*   \ \ \_\ \ /\ \/\ \  \ \ \   \ \ \_ \ \ \_\ \    \ \ \\ \  /\  __/ /\__, `\/\ \L\ \   \_\ \_ \ \ \_/ |/\  __/ )"<< "\n" <<
			R"(*    \ \_____\\ \_\ \_\  \ \_\   \ \__\ \/`____ \    \ \_\ \_\\ \____\\/\____/\ \____/   /\____\ \ \___/ \ \____\)"<< "\n" <<
			R"(*     \/_____/ \/_/\/_/   \/_/    \/__/  `/___/> \    \/_/\/ / \/____/ \/___/  \/___/    \/____/  \/__/   \/____/)"<< "\n" <<
			R"(*                                           /\___/                                                               )"<< "\n" <<
			R"(*                                           \/__/                                                                )"<< "\n" <<
			R"(*================================================================================================================)"
			<< "\n" << R"(*UnityResolve Library By 遂沫 2023/11/18-2023/11/21)" << " Mode:" << (static_cast<char>(mode_) ? "Mono" : "Il2cpp") << "\n*/" << '\n';

		for (const auto& [nAssembly, pAssembly] : assembly) {
			io << std::format("Assembly: {}\n", nAssembly.empty() ? "" : nAssembly);
			io << std::format("AssemblyFile: {} \n", pAssembly->file.empty() ? "" : pAssembly->file);
			io << "{\n\n";
			for (const auto& [nClass, pClass] : pAssembly->classes) {
				io << std::format("\tnamespace: {}", pClass->namespaze.empty() ? "" : pClass->namespaze);
				io << "\n";
				io << std::format("\tclass {}{} ", nClass, pClass->parent.empty() ? "" : " : " + pClass->parent);
				io << "{\n\n";
				for (const auto& [nField, pField] : pClass->fields) {
					io << std::format("\t\t{:+#06X} | {}{} {}\n", pField->offset, pField->static_field ? "static " : "", pField->type->name, nField);
				}
				io << "\n";
				for (const auto& [nMethod, pMethod] : pClass->methods) {
					io << std::format("\t\t[Flags: {:032b}] [ParamsCount: {:04d}] |RVA: {:+#010X}|\n", pMethod->flags, pMethod->args.size(), reinterpret_cast<std::uint64_t>(pMethod->function) - reinterpret_cast<std::uint64_t>(hmodule_));
					io << std::format("\t\t{}{} {}(", pMethod->static_function ? "static " : "", pMethod->return_type->name, nMethod);
					std::string params{};
					for (const auto& [nArg, pArg] : pMethod->args)
						params += std::format("{} {}, ", pArg->name, nArg);
					if (!params.empty()) {
						params.pop_back();
						params.pop_back();
					}
					io << (params.empty() ? "" : params) << ");\n\n";
				}
				io << "\t}\n\n";
			}
			io << "}\n\n";
		}

		io << '\n';
		io.close();
		SetFileAttributesA((file).c_str(), FILE_ATTRIBUTE_READONLY);
		SetFileAttributesA((file).c_str(), FILE_ATTRIBUTE_SYSTEM);
	}

	/**
	 * \brief 调用dll函数
	 * \tparam Return 返回类型 (必须)
	 * \tparam Args 参数类型 (可以忽略)
	 * \param funcName dll导出函数名称
	 * \param args 参数
	 * \return 模板类型
	 */
	template<typename Return, typename... Args>
	static auto Invoke(const std::string& funcName, Args... args) -> Return {
		static std::mutex mutex{};
		std::lock_guard   lock(mutex);

		// 检查函数是否已经获取地址, 没有则自动获取
		if (!address_.contains(funcName) || address_[funcName] == nullptr)
			address_[funcName] = static_cast<void*>(GetProcAddress(hmodule_, funcName.c_str()));

		if (address_[funcName] != nullptr)
			return reinterpret_cast<Return(*)(Args...)>(address_[funcName])(args...);
		throw std::logic_error("Not find function");
	}

	inline static std::unordered_map<std::string, Assembly*> assembly;


	class UnityType final {
	public:
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

			[[nodiscard]] auto  Normalize() const -> Vector3 {
				if (const float len = Length(); len > 0)
					return Vector3(x / len, y / len, z / len);
				return Vector3(x, y, z);
			}

			auto ToVectors(Vector3* m_pForward, Vector3* m_pRight, Vector3* m_pUp) const -> void {
				constexpr float m_fDeg2Rad = static_cast<float>(3.1415926) / 180.F;

				const float m_fSinX = sinf(x * m_fDeg2Rad);
				const float m_fCosX = cosf(x * m_fDeg2Rad);

				const float m_fSinY = sinf(y * m_fDeg2Rad);
				const float m_fCosY = cosf(y * m_fDeg2Rad);

				const float m_fSinZ = sinf(z * m_fDeg2Rad);
				const float m_fCosZ = cosf(z * m_fDeg2Rad);

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

			[[nodiscard]] auto Distance(const Vector3& event) const -> float {
				const float dx = this->x - event.x;
				const float dy = this->y - event.y;
				const float dz = this->z - event.z;
				return std::sqrt(dx * dx + dy * dy + dz * dz);
			}
		};

		struct Vector2 {
			float x, y;

			Vector2() { x = y = 0.f; }

			Vector2(const float f1, const float f2) {
				x = f1;
				y = f2;
			}

			[[nodiscard]] auto Distance(const Vector2& event) const -> float {
				const float dx = this->x - event.x;
				const float dy = this->y - event.y;
				return std::sqrt(dx * dx + dy * dy);
			}
		};

		struct Vector4 {
			float x, y, z, w;

			Vector4() { x = y = z = w = 0.F; }

			Vector4(const float f1, const float f2, const float f3, const float f4) {
				x = f1;
				y = f2;
				z = f3;
				w = f4;
			}
		};

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
				constexpr float m_fDeg2Rad = static_cast<float>(3.1415926) / 180.F;

				m_fX = m_fX * m_fDeg2Rad * 0.5F;
				m_fY = m_fY * m_fDeg2Rad * 0.5F;
				m_fZ = m_fZ * m_fDeg2Rad * 0.5F;

				const float m_fSinX = sinf(m_fX);
				const float m_fCosX = cosf(m_fX);

				const float m_fSinY = sinf(m_fY);
				const float m_fCosY = cosf(m_fY);

				const float m_fSinZ = sinf(m_fZ);
				const float m_fCosZ = cosf(m_fZ);

				x = m_fCosY * m_fSinX * m_fCosZ + m_fSinY * m_fCosX * m_fSinZ;
				y = m_fSinY * m_fCosX * m_fCosZ - m_fCosY * m_fSinX * m_fSinZ;
				z = m_fCosY * m_fCosX * m_fSinZ - m_fSinY * m_fSinX * m_fCosZ;
				w = m_fCosY * m_fCosX * m_fCosZ + m_fSinY * m_fSinX * m_fSinZ;

				return *this;
			}

			auto Euler(const Vector3& m_vRot) -> Quaternion { return Euler(m_vRot.x, m_vRot.y, m_vRot.z); }

			[[nodiscard]] auto ToEuler() const -> Vector3 {
				Vector3 m_vEuler;

				const float m_fDist = (x * x) + (y * y) + (z * z) + (w * w);

				if (const float m_fTest = x * w - y * z; m_fTest > 0.4995F * m_fDist) {
					m_vEuler.x = static_cast<float>(3.1415926) * 0.5F;
					m_vEuler.y = 2.F * atan2f(y, x);
					m_vEuler.z = 0.F;
				}
				else if (m_fTest < -0.4995F * m_fDist) {
					m_vEuler.x = static_cast<float>(3.1415926) * -0.5F;
					m_vEuler.y = -2.F * atan2f(y, x);
					m_vEuler.z = 0.F;
				}
				else {
					m_vEuler.x = asinf(2.F * (w * x - y * z));
					m_vEuler.y = atan2f(2.F * w * y + 2.F * z * x, 1.F - 2.F * (x * x + y * y));
					m_vEuler.z = atan2f(2.F * w * z + 2.F * x * y, 1.F - 2.F * (z * z + x * x));
				}

				constexpr float m_fRad2Deg = 180.F / static_cast<float>(3.1415926);
				m_vEuler.x *= m_fRad2Deg;
				m_vEuler.y *= m_fRad2Deg;
				m_vEuler.z *= m_fRad2Deg;

				return m_vEuler;
			}
		};

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

			explicit Color(const float fRed = 0.f,
				const float fGreen = 0.f,
				const float fBlue = 0.f,
				const float fAlpha = 1.f) {
				r = fRed;
				g = fGreen;
				b = fBlue;
				a = fAlpha;
			}
		};

		struct Matrix4x4 {
			float m[4][4] = { {0} };

			Matrix4x4() = default;

			auto operator[](const int i) -> float* { return m[i]; }
		};

		struct Object {
			union {
				void* klass{ nullptr };
				void* vtable;
			}         Il2CppClass;

			struct MonitorData* monitor{ nullptr };

			[[nodiscard]] auto GetClass() const -> void* { return this->Il2CppClass.klass; }
		};

		struct String : Object {
			int32_t m_stringLength{ 0 };
			wchar_t m_firstChar[32]{};

			[[nodiscard]] auto ToString() const -> std::string {
				std::string sRet(static_cast<size_t>(m_stringLength) * 3 + 1, '\0');
				WideCharToMultiByte(CP_UTF8,
					0,
					m_firstChar,
					m_stringLength,
					sRet.data(),
					static_cast<int>(sRet.size()),
					nullptr,
					nullptr);
				return sRet;
			}

			auto operator[](const int i) const -> wchar_t { return m_firstChar[i]; }

			auto Clear() -> void {
				memset(m_firstChar, 0, m_stringLength);
				m_stringLength = 0;
			}

			static auto New(const std::string& str) -> String* {
				if (mode_ == Mode::Il2cpp) {
					return UnityResolve::Invoke<String*, const char*>("il2cpp_string_new", str.c_str());
				}
				return UnityResolve::Invoke<String*, void*, const char*>("mono_string_new", UnityResolve::Invoke<void*>("mono_get_root_domain"), str.c_str());
			}
		};

		template<typename T>
		struct Array : Object {
			struct {
				std::uintptr_t length;
				std::int32_t   lower_bound;
			}*bounds{ nullptr };

			std::uintptr_t             max_length{ 0 };
			__declspec(align(8)) T* vector[32]{};

			auto GetData() -> uintptr_t {
				return reinterpret_cast<uintptr_t>(&vector);
			}

			auto operator[](unsigned int m_uIndex) -> T& {
				return *reinterpret_cast<T*>(GetData() + sizeof(T) * m_uIndex);
			}

			auto At(unsigned int m_uIndex) -> T& {
				return operator[](m_uIndex);
			}

			auto Insert(T* m_pArray, uintptr_t m_uSize, const uintptr_t m_uIndex = 0) -> void {
				if ((m_uSize + m_uIndex) >= max_length) {
					if (m_uIndex >= max_length)
						return;

					m_uSize = max_length - m_uIndex;
				}

				for (uintptr_t u = 0; m_uSize > u; ++u)
					operator[](u + m_uIndex) = m_pArray[u];
			}

			auto Fill(T m_tValue) -> void {
				for (uintptr_t u = 0; max_length > u; ++u)
					operator[](u) = m_tValue;
			}

			auto RemoveAt(const unsigned int m_uIndex) -> void {
				if (m_uIndex >= max_length)
					return;

				if (max_length > (m_uIndex + 1)) {
					for (unsigned int u = m_uIndex; (static_cast<unsigned int>(max_length) - m_uIndex) > u; ++u)
						operator[](u) = operator[](u + 1);
				}

				--max_length;
			}

			auto RemoveRange(unsigned int m_uIndex, unsigned int m_uCount) -> void {
				if (m_uCount == 0)
					m_uCount = 1;

				const unsigned int m_uTotal = m_uIndex + m_uCount;
				if (m_uTotal >= max_length)
					return;

				if (max_length > (m_uTotal + 1)) {
					for (unsigned int u = m_uIndex; (static_cast<unsigned int>(max_length) - m_uTotal) >= u; ++u)
						operator[](u) = operator[](u + m_uCount);
				}

				max_length -= m_uCount;
			}

			auto RemoveAll() -> void {
				if (max_length > 0) {
					memset(GetData(), 0, sizeof(Type) * max_length);
					max_length = 0;
				}
			}

			auto ToVector() -> std::vector<Type> {
				std::vector<Type> rs{};
				rs.reserve(this->max_length);
				for (int i = 0; i < this->max_length; i++)
					rs.push_back(this->At(i));
				return rs;
			}

			static auto New(const Class* kalss, const std::uintptr_t size) -> String* {
				if (mode_ == Mode::Il2cpp) {
					return UnityResolve::Invoke<Array*, void*, std::uintptr_t>("il2cpp_array_new", kalss->classinfo, size);
				}
				return UnityResolve::Invoke<Array*, void*, void*, std::uintptr_t>("mono_array_new", pDomain, kalss->classinfo, size);
			}
		};
			
		template<typename Type>
		struct List : Object {
			Array<Type>* pList;

			auto ToArray() -> Array<Type>* { return pList; }
		};

		template<typename TKey, typename TValue>
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

			auto GetEntry() -> Entry* { return static_cast<Entry*>(pEntries->GetData()); }

			auto GetKeyByIndex(const int iIndex) -> TKey {
				TKey tKey = { 0 };

				Entry* pEntry = GetEntry();
				if (pEntry)
					tKey = pEntry[iIndex].m_tKey;

				return tKey;
			}

			auto GetValueByIndex(const int iIndex) -> TValue {
				TValue tValue = { 0 };

				Entry* pEntry = GetEntry();
				if (pEntry)
					tValue = pEntry[iIndex].m_tValue;

				return tValue;
			}

			auto GetValueByKey(const TKey tKey) -> TValue {
				TValue tValue = { 0 };
				for (int i = 0; i < iCount; i++) {
					if (GetEntry()[i].m_tKey == tKey)
						tValue = GetEntry()[i].m_tValue;
				}
				return tValue;
			}

			auto operator[](const TKey tKey) const -> TValue {
				return GetValueByKey(tKey);
			}
		};

		struct Camera {
			enum class Eye : int {
				Left,
				Right,
				Mono
			};

			static auto GetMain() -> Camera* {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Camera"]->methods["get_main"];

				if (method)
					return method->Invoke<Camera*>();
				throw std::logic_error("nullptr");
			}

			auto GetDepth() -> float {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Camera"]->methods["get_depth"];

				if (method)
					return method->Invoke<float>(this);
				throw std::logic_error("nullptr");
			}

			auto SetDepth(const float depth) -> void {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Camera"]->methods["set_depth"];

				if (method)
					return method->Invoke<void>(this, depth);
			}

			auto WorldToScreenPoint(const Vector3& position, const Eye eye) -> Vector3 {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Camera"]->methods[mode_ == Mode::Mono ? "WorldToScreenPoint_Injected" : "WorldToScreenPoint"];
				if (mode_ == Mode::Mono) {
					Vector3 vec3{};
					method->Invoke<void>(this, position, eye, &vec3);
					return vec3;
				}
				if (method)
					return method->Invoke<Vector3>(this, position, eye);
				throw std::logic_error("nullptr");
			}

			auto ScreenToWorldPoint(const Vector3& position, const Eye eye) -> Vector3 {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Camera"]->methods[mode_ == Mode::Mono ? "ScreenToWorldPoint_Injected" : "ScreenToWorldPoint"];
				if (mode_ == Mode::Mono) {
					Vector3 vec3{};
					method->Invoke<void>(this, position, eye, &vec3);
					return vec3;
				}
				if (method)
					return method->Invoke<Vector3>(this, position, eye);
				throw std::logic_error("nullptr");
			}
		};
		 
		struct Transform {
			auto GetPosition() -> Vector3 {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Transform"]->methods[mode_ == Mode::Mono ? "get_position_Injected" : "get_position"];
				if (mode_ == Mode::Mono) {
					Vector3 vec3{};
					if (method)
						method->Invoke<void>(this, &vec3);
					return vec3;
				}
				if (method)
					return method->Invoke<Vector3>(this);
				return {};
			}

			auto SetPosition(const Vector3& position) -> Vector3 {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Transform"]->methods[mode_ == Mode::Mono ? "set_position_Injected" : "set_position"];
				if (mode_ == Mode::Mono) {
					Vector3 vec3{};
					if (method)
						method->Invoke<void>(this, &vec3);
					return vec3;
				}
				if (method)
					return method->Invoke<Vector3>(this, position);
				throw std::logic_error("nullptr");
			}

			auto GetChildCount() -> int {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Transform"]->methods["get_childCount"];

				if (method)
					return method->Invoke<int>(this);
				throw std::logic_error("nullptr");
			}

			auto GetChild(const int index) -> Transform* {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Transform"]->methods["GetChild"];

				if (method)
					return method->Invoke<Transform*>(this, index);
				throw std::logic_error("nullptr");
			}
		};

		struct Component {
			auto GetTransform() -> Transform* {
				static Method* method;
				if (!method)
					method = assembly["UnityEngine.CoreModule"]->classes["Component"]->methods["get_transform"];

				if (method)
					return method->Invoke<Transform*>(this);
				throw std::logic_error("nullptr");
			}
		};

		struct LayerMask {
			
		};

		struct Rigidbody {
			
		};

	private:
		template<typename Return, typename... Args>
		static auto Invoke(const void* address, Args... args) -> Return {
			if (address != nullptr)
				return reinterpret_cast<Return(*)(Args...)>(address)(args...);
			throw std::logic_error("nullptr");
		}
	};

private:
	inline static Mode                         mode_{};
	inline static HMODULE                      hmodule_;
	inline static std::unordered_map<std::string, void*> address_{};
	inline static void* pDomain{};
};
#endif // UNITYRESOLVE_HPP