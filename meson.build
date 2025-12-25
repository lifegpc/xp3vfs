project('xp3vfs', default_options : ['cpp_std=c++17'])
add_languages('c', 'cpp', required: true)

cc = meson.get_compiler('c')
deps = []
conf = configuration_data()
WIN32 = host_machine.system() in ['windows', 'cygwin']
MSVC = cc.get_id() == 'msvc'

if MSVC
    add_project_arguments('/utf-8', language: 'c')
    add_project_arguments('/utf-8', language: 'cpp')
endif

utils_default_options = []
if not WIN32
    utils_default_options += 'utils_iconv=enabled'
endif

if get_option('wrap')
    zlib = subproject('zlib', required: true)
    zlib_dep = zlib.get_variable('zlib_dep')
    deps += zlib_dep
    if get_option('zstd')
        zstd = subproject('zstd', required: true, default_options: ['bin_programs=false'])
        zstd_dep = zstd.get_variable('libzstd_dep')
        deps += zstd_dep
        conf.set('HAVE_ZSTD', 1)
    endif
else
    zlib_dep = dependency('zlib', required: true, version: '>=1.1.2')
    deps += zlib_dep
    if get_option('zstd')
        zstd_dep = dependency('libzstd', required: false, version: '>=1.4.4')
        if zstd_dep.found()
            deps += zstd_dep
            conf.set('HAVE_ZSTD', 1)
        endif
    endif
endif

utils_dep = dependency('utils', required: false)
if not utils_dep.found()
    utils = subproject('utils', required: true, default_options: utils_default_options)
    utils_dep = utils.get_variable('utils_dep')
endif
deps += utils_dep

configure_file(output: 'xp3vfs_config.h', configuration: conf)

src = files([
    'xp3.h',
    'xp3.cpp',
    'decompressor.h',
    'decompressor.cpp',
])

xp3vfs = static_library('xp3vfs',
    src,
    dependencies: deps,
    pic: true,
)
xp3vfs_dep = declare_dependency(include_directories: include_directories('.'), link_with: xp3vfs)

if get_option('cli')
    exe_src = files([
        'cli.cpp',
    ])
    cli_deps = [xp3vfs_dep, utils_dep, zlib_dep]
    executable('xp3vfs-cli',
        exe_src,
        dependencies: cli_deps,
    )
endif
