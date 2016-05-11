#include "Module.h"

#include <fstream>

#include "CodeGen_C.h"
#include "Debug.h"
#include "LLVM_Headers.h"
#include "LLVM_Output.h"
#include "LLVM_Runtime_Linker.h"
#include "Outputs.h"
#include "StmtToHtml.h"

namespace Halide {

namespace Internal {

struct ModuleContents {
    mutable RefCount ref_count;
    std::string name;
    Target target;
    std::vector<Buffer> buffers;
    std::vector<Internal::LoweredFunc> functions;
};

template<>
EXPORT RefCount &ref_count<ModuleContents>(const ModuleContents *f) {
    return f->ref_count;
}

template<>
EXPORT void destroy<ModuleContents>(const ModuleContents *f) {
    delete f;
}

}  // namespace Internal

Module::Module(const std::string &name, const Target &target) :
    contents(new Internal::ModuleContents) {
    contents.ptr->name = name;
    contents.ptr->target = target;
}

const Target &Module::target() const {
    return contents.ptr->target;
}

const std::string &Module::name() const {
    return contents.ptr->name;
}

const std::vector<Buffer> &Module::buffers() const {
    return contents.ptr->buffers;
}

const std::vector<Internal::LoweredFunc> &Module::functions() const {
    return contents.ptr->functions;
}

void Module::append(const Buffer &buffer) {
    contents.ptr->buffers.push_back(buffer);
}

void Module::append(const Internal::LoweredFunc &function) {
    contents.ptr->functions.push_back(function);
}

Module link_modules(const std::string &name, const std::vector<Module> &modules) {
    Module output(name, modules.front().target());

    for (size_t i = 0; i < modules.size(); i++) {
        const Module &input = modules[i];

        if (output.target() != input.target()) {
            user_error << "Mismatched targets in modules to link ("
                       << output.name() << ", " << output.target().to_string()
                       << "), ("
                       << input.name() << ", " << input.target().to_string() << ")\n";
        }

        // TODO(dsharlet): Check for naming collisions, maybe rename
        // internal linkage declarations in the case of collision.
        for (const auto &b : input.buffers()) {
            output.append(b);
        }
        for (const auto &f : input.functions()) {
            output.append(f);
        }
    }

    return output;
}

void Module::compile(const Outputs &output_files) const {
    if (!output_files.object_name.empty() || !output_files.assembly_name.empty() ||
        !output_files.bitcode_name.empty() || !output_files.llvm_assembly_name.empty()) {
        llvm::LLVMContext context;
        std::unique_ptr<llvm::Module> llvm_module(compile_module_to_llvm_module(*this, context));

        if (!output_files.object_name.empty()) {
            if (target().arch == Target::PNaCl) {
                compile_llvm_module_to_llvm_bitcode(*llvm_module, output_files.object_name);
            } else {
                compile_llvm_module_to_object(*llvm_module, output_files.object_name);
            }
        }
        if (!output_files.assembly_name.empty()) {
            if (target().arch == Target::PNaCl) {
                compile_llvm_module_to_llvm_assembly(*llvm_module, output_files.assembly_name);
            } else {
                compile_llvm_module_to_assembly(*llvm_module, output_files.assembly_name);
            }
        }
        if (!output_files.bitcode_name.empty()) {
            compile_llvm_module_to_llvm_bitcode(*llvm_module, output_files.bitcode_name);
        }
        if (!output_files.llvm_assembly_name.empty()) {
            compile_llvm_module_to_llvm_assembly(*llvm_module, output_files.llvm_assembly_name);
        }
    }
    if (!output_files.c_header_name.empty()) {
        std::ofstream file(output_files.c_header_name.c_str());
        Internal::CodeGen_C cg(file,
                               target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusHeader : Internal::CodeGen_C::CHeader,
                               output_files.c_header_name);
        cg.compile(*this);
    }
    if (!output_files.c_source_name.empty()) {
        std::ofstream file(output_files.c_source_name.c_str());
        Internal::CodeGen_C cg(file,
                               target().has_feature(Target::CPlusPlusMangling) ?
                               Internal::CodeGen_C::CPlusPlusImplementation : Internal::CodeGen_C::CImplementation);
        cg.compile(*this);
    }
    if (!output_files.stmt_name.empty()) {
        std::ofstream file(output_files.stmt_name.c_str());
        file << *this;
    }
    if (!output_files.stmt_html_name.empty()) {
        Internal::print_to_html(output_files.stmt_html_name, *this);
    }
}

void compile_standalone_runtime(std::string object_filename, Target t) {
    Module empty("standalone_runtime", t.without_feature(Target::NoRuntime).without_feature(Target::JIT));
    empty.compile(Outputs().object(object_filename));
}

}
