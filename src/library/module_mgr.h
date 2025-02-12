/*
Copyright (c) 2016 Microsoft Corporation. All rights reserved.
Released under Apache 2.0 license as described in the file LICENSE.

Author: Gabriel Ebner
*/
#pragma once
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "frontends/lean/module_parser.h"
#include "util/unit.h"
#include "util/name.h"
#include "kernel/environment.h"
#include "util/task.h"
#include "library/io_state.h"
#include "library/trace.h"
#include "frontends/lean/parser.h"
#include "util/lean_path.h"

namespace lean {

enum class module_src {
    OLEAN,
    LEAN,
};

struct module_info {
    bool m_out_of_date = false;

    module_id m_id;
    std::string m_contents;
    // Hash of the Lean source (after normalizing line endings) yielding this module:
    // - if m_source == LEAN, hash_data(remove_cr(m_contents))
    // - if m_source == OLEAN, value loaded from the .olean
    unsigned m_src_hash;
    // Transitive hash of all source code this module was built from,
    // i.e. m_src_hash mixed with the m_trans_hash of each imported module
    unsigned m_trans_hash;
    module_src m_source = module_src::LEAN;

    struct dependency {
        module_id m_id;
        module_name m_import_name;
        std::shared_ptr<module_info const> m_mod_info;
    };
    std::vector<dependency> m_deps;

    struct parse_result {
        options               m_opts;
        std::shared_ptr<loaded_module const> m_loaded_module;
    };
    task<parse_result> m_result;
    gtask m_ast_export;

    optional<module_parser_result> m_snapshots;

    gtask m_olean_task;

    cancellation_token m_cancel;
    log_tree::node m_lt;

    environment const & get_produced_env() const {
        return get(get(m_result).m_loaded_module->m_env);
    }

    environment get_latest_env() const;

    module_info() {}

    module_info(module_id const & id, std::string const & contents, unsigned src_hash, unsigned trans_hash, module_src src)
            : m_id(id), m_contents(contents), m_src_hash(src_hash), m_trans_hash(trans_hash), m_source(src) {}
};

/* A virtual interface for loading Lean modules by name. It has two instantiations:
- `fs_module_vs` for loading modules from disk
- `server` for loading modules open in an editor */
class module_vfs {
public:
    virtual ~module_vfs() {}
    // need to support changed lean dependencies of olean files
    // need to support changed editor dependencies of olean files
    virtual std::shared_ptr<module_info> load_module(module_id const &, bool can_use_olean) = 0;
};

class fs_module_vfs : public module_vfs {
public:
    std::unordered_set<module_id> m_modules_to_load_from_source;
    std::shared_ptr<module_info> load_module(module_id const & id, bool can_use_olean) override;
};

class module_mgr {
    bool m_server_mode = false;
    bool m_save_olean = false;
    bool m_use_old_oleans = false;
    bool m_report_widgets = true;
    bool m_export_ast = false;

    search_path m_path;
    environment m_initial_env;
    io_state m_ios;
    module_vfs * m_vfs;
    log_tree::node m_lt;

    mutex m_mutex;
    std::unordered_map<module_id, std::shared_ptr<module_info>> m_modules;

    void mark_out_of_date(module_id const & id);
    void build_module(module_id const & id, bool can_use_olean, name_set module_stack);

    std::vector<module_name> get_direct_imports(module_id const & id, std::string const & contents);
    void build_lean(std::shared_ptr<module_info> const & mod, name_set const & module_stack);
    std::pair<cancellation_token, module_parser_result>
    build_lean_snapshots(std::shared_ptr<module_parser> const & mod_parser,
                         std::shared_ptr<module_info> const & old_mod, std::vector<gtask> const & deps,
                         std::string const & contents);

public:
    module_mgr(module_vfs * vfs, log_tree::node const & lt,
               search_path const & path,
               environment const & initial_env, io_state const & ios) :
            m_path(path), m_initial_env(initial_env), m_ios(ios), m_vfs(vfs), m_lt(lt) {}

    void invalidate(module_id const & id);

    module_id resolve(module_name const & n, module_id const & cur_mod);

    std::shared_ptr<module_info const> get_module(module_id const &);

    std::vector<std::shared_ptr<module_info const>> get_all_modules();

    void cancel_all();

    void set_server_mode(bool use_snapshots) { m_server_mode = use_snapshots; }
    bool get_server_mode() const { return m_server_mode; }

    void set_save_olean(bool save_olean) { m_save_olean = save_olean; }
    bool get_save_olean() const { return m_save_olean; }

    void set_use_old_oleans(bool use_old_oleans) { m_use_old_oleans = use_old_oleans; }
    bool get_use_old_oleans() const { return m_use_old_oleans; }
    void set_report_widgets(bool report_widgets) { m_report_widgets = report_widgets; }
    bool get_report_widgets() const { return m_report_widgets; }
    void set_export_ast(bool export_ast) { m_export_ast = export_ast; }

    environment get_initial_env() const { return m_initial_env; }
    options get_options() const { return m_ios.get_options(); }
    io_state get_io_state() const { return m_ios; }
    void set_log_tree(log_tree::node const & lt) { m_lt = lt; }
};

environment get_combined_environment(environment const & env0,
                                     buffer<std::shared_ptr<module_info const>> const & mods);

module_loader mk_loader(module_id const & cur_mod, std::vector<module_info::dependency> const & deps);

module_mgr * get_global_module_mgr();
void set_global_module_mgr(module_mgr & mgr);

}
