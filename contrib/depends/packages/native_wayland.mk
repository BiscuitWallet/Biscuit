package=native_wayland
$(package)_version=1.24.0
$(package)_download_path := https://gitlab.freedesktop.org/wayland/wayland/-/archive/$($(package)_version)/
$(package)_file_name := wayland-$($(package)_version).tar.gz
$(package)_sha256_hash := 7800858844751fc7113d7df3678dc6b58b26a056176a65c49a059763045bffd5
$(package)_dependencies := native_expat native_libffi

define $(package)_config_cmds
  meson setup build -Dprefix="$(build_prefix)" -Ddtd_validation=false -Ddocumentation=false -Dtests=false
endef

define $(package)_build_cmds
  ninja -C build
endef

define $(package)_stage_cmds
  DESTDIR=$($(package)_staging_dir) ninja -C build install
endef
