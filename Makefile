CC := gcc
STRIP ?= strip
CFLAGS ?= -O2 -g
WARNINGS := -Wall -Wextra -Werror -Wpedantic -Wformat=2 -Wundef -Wshadow \
	-Wmissing-prototypes -Wstrict-prototypes -Wold-style-definition
BUILD_CFLAGS := -std=c17 $(WARNINGS) -fPIC -fdebug-prefix-map=$(abspath .)=. \
	-ffile-prefix-map=$(abspath .)=. -fmacro-prefix-map=$(abspath .)=. \
	-Ibuild $(CFLAGS)
HELPER_CFLAGS := -std=c17 $(WARNINGS) -Os -fPIE \
	-fdebug-prefix-map=$(abspath .)=. -ffile-prefix-map=$(abspath .)=. \
	-fmacro-prefix-map=$(abspath .)=.
MINGW_CFLAGS := -std=c17 $(WARNINGS)
PREFIX ?= $(HOME)/.local

STATE_DIRECTORY_SRC := src/state_directory.c
THREAD_CPU_SYSTEMD_SRC := src/thread_cpu_systemd.c
THREAD_CPU_EXTERNAL_SRC := src/thread_cpu_external.c
PACER_SRC := \
	src/effective_config_report.c \
	src/pacer_clock.c \
	src/pacer_limit.c \
	src/thread_cpu_quota.c \
	$(THREAD_CPU_EXTERNAL_SRC) \
	src/thread_cpu_protocol.c \
	$(THREAD_CPU_SYSTEMD_SRC) \
	$(STATE_DIRECTORY_SRC)
LOG_RETENTION_SRC := src/log_retention.c
NVML_SRC := \
	src/hud_nvml_client.c \
	src/hud_nvml_protocol.c \
	src/hud_nvml_provider.c
HUD_SRC := \
	src/hud_drm_fdinfo.c \
	src/hud_font.c \
	src/hud_fps.c \
	src/hud_metrics.c \
	src/hud_metrics_cache.c \
	$(NVML_SRC) \
	src/hud_text.c \
	src/hud_vertices.c
VERSION_HEADER := build/frame_pacer_version.h
VULKAN_SRC := \
	src/frame_pacer_layer.c \
	src/vulkan_layer_registry.c \
	src/vulkan_layer_hud.c \
	$(PACER_SRC) \
	$(LOG_RETENTION_SRC) \
	src/pacer_queue.c \
	src/pacer_compatibility.c \
	$(HUD_SRC) \
	src/hud_vulkan_resources.c \
	src/hud_swapchain_policy.c \
	src/hud_vulkan_commands.c \
	src/hud_vulkan_device.c \
	src/hud_vulkan_draw_resources.c \
	src/hud_vulkan_pipeline.c \
	src/hud_vulkan_vertex_buffer.c \
	src/hud_vulkan_record.c \
	src/hud_vulkan_present.c
GL_SRC := src/gl_pacer_interposer.c src/gl_pacer_dispatch.c \
	src/gl_hud_renderer.c $(PACER_SRC) $(LOG_RETENTION_SRC) $(HUD_SRC)
HDRS := $(wildcard src/*.h)
VULKAN_ARTIFACTS := \
	build/x86_64/libVkLayer_frame_pacer.so \
	build/i386/libVkLayer_frame_pacer.so \
	build/x86_64/layer/VkLayer_frame_pacer.json \
	build/i386/layer/VkLayer_frame_pacer.json \
	build/x86_64/implicit_layer/VkLayer_frame_pacer.x86_64.json \
	build/i386/implicit_layer/VkLayer_frame_pacer.i386.json
CONTROLLER_ARTIFACT := build/frame-pacer-thread-cpu-controller
GL_ARTIFACTS := \
	build/x86_64/libframe_pacer_gl.so \
	build/i386/libframe_pacer_gl.so \
	build/x86_64/libframe_pacer_gl_shim.so \
	build/i386/libframe_pacer_gl_shim.so
GL_RUNTIME_ARTIFACTS := \
	build/lib/x86_64-linux-gnu/libframe_pacer_gl.so \
	build/lib/i386-linux-gnu/libframe_pacer_gl.so \
	build/lib/x86_64-linux-gnu/libframe_pacer_gl_shim.so \
	build/lib/i386-linux-gnu/libframe_pacer_gl_shim.so \
	build/lib/libframe_pacer_gl.so \
	build/lib32/libframe_pacer_gl.so \
	build/lib/libframe_pacer_gl_shim.so \
	build/lib32/libframe_pacer_gl_shim.so

.PHONY: all check check-unit check-unit-i386 check-shell check-docs check-workflows check-hud-image check-hot-path-stack check-abi check-analyzer check-sanitize check-tsan check-coverage benchmark-performance docs-hud-image \
	clean install install-payload uninstall release-package check-release-package \
	thread-cpu-quota-probe run-thread-cpu-quota-probe \
	thread-cpu-quota-controller-integration run-thread-cpu-quota-controller-integration \
	thread-cpu-quota-controller-integration-i386 run-thread-cpu-quota-controller-integration-i386 \
	dxgi-forward-probe hud-shaders metrics-probe pci-probe research winepath-probe \
	run-nvml-helper-probe \
	vulkan-present-probe run-vulkan-present-probe glx-present-probe \
	run-glx-present-probe egl-present-probe run-egl-present-probe

all: $(VULKAN_ARTIFACTS) $(CONTROLLER_ARTIFACT)
docs-hud-image: build/generated-frame-pacer-hud.png
	cp $< docs/images/frame-pacer-hud.png
check-hud-image: build/generated-frame-pacer-hud.png
	cmp $< docs/images/frame-pacer-hud.png
build/generated-frame-pacer-hud.png: build/render-hud-image
	./build/render-hud-image $@
build/render-hud-image: tests/render_hud_image.c src/hud_text.c src/hud_text.h src/hud_metrics.h src/pacer_limit.h src/hud_vertices.c src/hud_vertices.h src/hud_font.c src/hud_font.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/render_hud_image.c src/hud_text.c src/hud_vertices.c src/hud_font.c
install: install-payload
	FRAME_PACER_PAYLOAD_DIR="$(abspath build/install-payload)" \
		PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" sh packaging/install.sh
install-payload: build/install-payload/.ready
build/install-payload/.ready: $(VULKAN_ARTIFACTS) $(GL_ARTIFACTS) $(CONTROLLER_ARTIFACT) \
		VkLayer_frame_pacer_implicit.json.in VERSION
	rm -rf build/install-payload
	install -d build/install-payload/x86_64 build/install-payload/i386
	install -m 0755 build/x86_64/libVkLayer_frame_pacer.so \
		build/x86_64/libframe_pacer_gl.so \
		build/x86_64/libframe_pacer_gl_shim.so \
		build/install-payload/x86_64/
	install -m 0755 build/i386/libVkLayer_frame_pacer.so \
		build/i386/libframe_pacer_gl.so \
		build/i386/libframe_pacer_gl_shim.so \
		build/install-payload/i386/
	install -m 0755 $(CONTROLLER_ARTIFACT) build/install-payload/
	install -m 0644 VkLayer_frame_pacer_implicit.json.in VERSION \
		build/install-payload/
	touch $@
uninstall:
	PREFIX="$(PREFIX)" DESTDIR="$(DESTDIR)" sh packaging/uninstall.sh
release-package: check install-payload
	STRIP="$(STRIP)" sh packaging/package.sh
check-release-package: release-package
	sh tests/test_release_package.sh
$(CONTROLLER_ARTIFACT): src/thread_cpu_quota_controller.c src/thread_cpu_protocol.c src/thread_cpu_protocol.h $(THREAD_CPU_SYSTEMD_SRC) src/thread_cpu_systemd.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ src/thread_cpu_quota_controller.c src/thread_cpu_protocol.c $(THREAD_CPU_SYSTEMD_SRC) -ldl
	chmod 0755 $@
build/frame-pacer-thread-cpu-controller-test: src/thread_cpu_quota_controller.c src/thread_cpu_protocol.c src/thread_cpu_protocol.h $(THREAD_CPU_SYSTEMD_SRC) src/thread_cpu_systemd.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ src/thread_cpu_quota_controller.c src/thread_cpu_protocol.c $(THREAD_CPU_SYSTEMD_SRC) -ldl
	chmod 0755 $@
thread-cpu-quota-probe: build/test-thread-cpu-quota-probe
build/test-thread-cpu-quota-probe: tests/thread_cpu_quota_probe.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ $< -ldl -pthread
run-thread-cpu-quota-probe: build/test-thread-cpu-quota-probe
	FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION=1 sh ./tests/thread_cpu_quota_probe.sh
thread-cpu-quota-controller-integration: build/test-thread-cpu-quota-controller-integration
build/test-thread-cpu-quota-controller-integration: tests/thread_cpu_quota_controller_integration.c src/thread_cpu_quota.c src/thread_cpu_quota.h $(THREAD_CPU_EXTERNAL_SRC) src/thread_cpu_external.h src/thread_cpu_protocol.c src/thread_cpu_protocol.h $(THREAD_CPU_SYSTEMD_SRC) src/thread_cpu_systemd.h $(STATE_DIRECTORY_SRC) src/state_directory.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/thread_cpu_quota_controller_integration.c src/thread_cpu_quota.c $(THREAD_CPU_EXTERNAL_SRC) src/thread_cpu_protocol.c $(THREAD_CPU_SYSTEMD_SRC) $(STATE_DIRECTORY_SRC) -ldl -pthread
run-thread-cpu-quota-controller-integration: build/test-thread-cpu-quota-controller-integration
	FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION=1 sh ./tests/thread_cpu_quota_controller_integration.sh
thread-cpu-quota-controller-integration-i386: build/test-thread-cpu-quota-controller-integration-i386
build/test-thread-cpu-quota-controller-integration-i386: tests/thread_cpu_quota_controller_integration.c src/thread_cpu_quota.c src/thread_cpu_quota.h $(THREAD_CPU_EXTERNAL_SRC) src/thread_cpu_external.h src/thread_cpu_protocol.c src/thread_cpu_protocol.h $(THREAD_CPU_SYSTEMD_SRC) src/thread_cpu_systemd.h $(STATE_DIRECTORY_SRC) src/state_directory.h
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ tests/thread_cpu_quota_controller_integration.c src/thread_cpu_quota.c $(THREAD_CPU_EXTERNAL_SRC) src/thread_cpu_protocol.c $(THREAD_CPU_SYSTEMD_SRC) $(STATE_DIRECTORY_SRC) -ldl -pthread
run-thread-cpu-quota-controller-integration-i386: build/test-thread-cpu-quota-controller-integration-i386 build/frame-pacer-thread-cpu-controller-test
	FRAME_PACER_THREAD_CPU_QUOTA_INTEGRATION=1 \
	FRAME_PACER_TEST_THREAD_CPU_HELPER="$(abspath build/frame-pacer-thread-cpu-controller-test)" \
	FRAME_PACER_TEST_CONTROLLER_FAIL_WRITES_WHEN="$(abspath build/thread-cpu-write-failure.trigger)" \
	sh ./tests/thread_cpu_quota_controller_integration.sh ./build/test-thread-cpu-quota-controller-integration-i386
build/windows/x86_64/frame_pacer_probe.dll: tests/wine_probe_dll.c
	mkdir -p $(@D)
	x86_64-w64-mingw32-gcc $(MINGW_CFLAGS) -shared -o $@ $<
build/windows/i386/frame_pacer_probe.dll: tests/wine_probe_dll.c
	mkdir -p $(@D)
	i686-w64-mingw32-gcc $(MINGW_CFLAGS) -shared -o $@ $<
build/windows/probe-client/frame_pacer_probe.exe: tests/wine_probe_client.c
	mkdir -p $(@D)
	x86_64-w64-mingw32-gcc $(MINGW_CFLAGS) -municode -o $@ $<
winepath-probe: build/windows/x86_64/frame_pacer_probe.dll build/windows/i386/frame_pacer_probe.dll build/windows/probe-client/frame_pacer_probe.exe
	./tests/test_winepath.sh
build/windows/x86_64/dxgi.dll: tests/dxgi_proxy_probe.c
	mkdir -p $(@D)
	x86_64-w64-mingw32-gcc $(MINGW_CFLAGS) -shared -o $@ $<
build/windows/dxgi-proxy-client/dxgi_proxy_probe.exe: tests/dxgi_proxy_client.c
	mkdir -p $(@D)
	x86_64-w64-mingw32-gcc $(MINGW_CFLAGS) -municode -o $@ $<
dxgi-forward-probe: build/windows/x86_64/dxgi.dll build/windows/dxgi-proxy-client/dxgi_proxy_probe.exe
	./tests/test_dxgi_forward.sh
$(VERSION_HEADER): VERSION packaging/read-version.sh packaging/generate-version-header.sh
	mkdir -p $(@D)
	sh packaging/generate-version-header.sh VERSION $@
build/x86_64/libVkLayer_frame_pacer.so: $(VULKAN_SRC) $(HDRS) build/hud_spv.h $(VERSION_HEADER)
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $(VULKAN_SRC) -ldl -pthread
build/i386/libVkLayer_frame_pacer.so: $(VULKAN_SRC) $(HDRS) build/hud_spv.h build/hud_nvml_helper_image.h $(VERSION_HEADER)
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $(VULKAN_SRC) -ldl -pthread
build/x86_64/libframe_pacer_gl.so: $(GL_SRC) $(HDRS) $(VERSION_HEADER)
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $(GL_SRC) -ldl -pthread
build/i386/libframe_pacer_gl.so: $(GL_SRC) $(HDRS) build/hud_nvml_helper_image.h $(VERSION_HEADER)
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $(GL_SRC) -ldl -pthread
build/x86_64/libframe_pacer_gl_shim.so: src/gl_pacer_shim.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $< -ldl -pthread
build/i386/libframe_pacer_gl_shim.so: src/gl_pacer_shim.c
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -shared -Wl,-Bsymbolic -o $@ $< -ldl -pthread
build/frame-pacer-nvml-helper: src/hud_nvml_helper.c src/hud_nvml_protocol.c src/hud_nvml_protocol.h src/hud_nvml_provider.c src/hud_nvml_provider.h
	mkdir -p $(@D)
	$(CC) -m64 $(HELPER_CFLAGS) -Isrc -Wl,-s -pie -o $@ \
		src/hud_nvml_helper.c src/hud_nvml_protocol.c src/hud_nvml_provider.c -ldl
	test "$$(stat -c %s $@)" -le 65536
build/hud_nvml_helper_image.h: build/frame-pacer-nvml-helper Makefile
	{ echo '#ifndef FRAME_PACER_HUD_NVML_HELPER_IMAGE_H'; \
	  echo '#define FRAME_PACER_HUD_NVML_HELPER_IMAGE_H'; \
	  echo 'static const unsigned char frame_pacer_nvml_helper_image[] = {'; \
	  xxd -i < $<; \
	  echo '};'; \
	  echo 'static const unsigned int frame_pacer_nvml_helper_image_len = sizeof(frame_pacer_nvml_helper_image);'; \
	  echo '#endif'; } > $@
build/lib/x86_64-linux-gnu/%.so: build/x86_64/%.so
	mkdir -p $(@D)
	cp $< $@
build/lib/i386-linux-gnu/%.so: build/i386/%.so
	mkdir -p $(@D)
	cp $< $@
build/lib/%.so: build/x86_64/%.so
	mkdir -p $(@D)
	cp $< $@
build/lib32/%.so: build/i386/%.so
	mkdir -p $(@D)
	cp $< $@
build/x86_64/libGL.so.1: tests/gl_swap_provider.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -shared -o $@ $<
build/i386/libGL.so.1: tests/gl_swap_provider.c
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -shared -o $@ $<
build/test-gl-pacer: tests/test_gl_pacer.c build/x86_64/libGL.so.1
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ tests/test_gl_pacer.c -ldl
build/test-gl-pacer-i386: tests/test_gl_pacer.c build/i386/libGL.so.1
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -o $@ tests/test_gl_pacer.c -ldl
build/test-gl-shim-noop: tests/test_gl_shim_noop.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ $< -ldl
build/test-gl-shim-noop-i386: tests/test_gl_shim_noop.c
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -o $@ $< -ldl
research: $(GL_ARTIFACTS) winepath-probe dxgi-forward-probe \
	build/smoke build/smoke-i386 build/smoke-device metrics-probe pci-probe
build/%/layer/VkLayer_frame_pacer.json: VkLayer_frame_pacer.json.in
	mkdir -p $(@D)
	cp $< $@
build/x86_64/implicit_layer/VkLayer_frame_pacer.x86_64.json: VkLayer_frame_pacer_implicit.json.in build/x86_64/libVkLayer_frame_pacer.so
	mkdir -p $(@D)
	sed -e 's|@LIBRARY_PATH@|$(abspath build/x86_64/libVkLayer_frame_pacer.so)|' -e 's|@LAYER_NAME@|VK_LAYER_ENDJYNN_frame_pacer_x86_64|' $< > $@
build/i386/implicit_layer/VkLayer_frame_pacer.i386.json: VkLayer_frame_pacer_implicit.json.in build/i386/libVkLayer_frame_pacer.so
	mkdir -p $(@D)
	sed -e 's|@LIBRARY_PATH@|$(abspath build/i386/libVkLayer_frame_pacer.so)|' -e 's|@LAYER_NAME@|VK_LAYER_ENDJYNN_frame_pacer_i386|' $< > $@
build/test_pacer_clock: tests/test_pacer_clock.c src/pacer_clock.c src/pacer_limit.c src/pacer_clock.h src/pacer_limit.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_pacer_clock.c src/pacer_clock.c src/pacer_limit.c -pthread
build/test_pacer_limit: tests/test_pacer_limit.c src/pacer_limit.c src/pacer_limit.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ tests/test_pacer_limit.c src/pacer_limit.c -pthread
build/test_effective_config_report: tests/test_effective_config_report.c src/effective_config_report.c src/effective_config_report.h src/pacer_limit.c src/pacer_limit.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_effective_config_report.c src/effective_config_report.c src/pacer_limit.c -pthread
build/test_log_retention: tests/test_log_retention.c $(LOG_RETENTION_SRC) src/log_retention.h $(STATE_DIRECTORY_SRC) src/state_directory.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_LOG_LIMIT=64U -Isrc -o $@ tests/test_log_retention.c $(LOG_RETENTION_SRC) $(STATE_DIRECTORY_SRC) -pthread
build/benchmark-pacer-limit: tests/benchmark_pacer_limit.c src/pacer_limit.c src/pacer_limit.h $(LOG_RETENTION_SRC) src/log_retention.h $(STATE_DIRECTORY_SRC) src/state_directory.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/benchmark_pacer_limit.c src/pacer_limit.c $(LOG_RETENTION_SRC) $(STATE_DIRECTORY_SRC) -pthread
build/benchmark-gl-present: tests/benchmark_gl_present.c build/x86_64/libGL.so.1
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ $< -Lbuild/x86_64 \
		-Wl,-rpath,$(abspath build/x86_64) -l:libGL.so.1
build/benchmark-hud-metrics-cache: tests/benchmark_hud_metrics_cache.c src/hud_metrics_cache.c src/hud_metrics_cache.h src/hud_metrics.c src/hud_metrics.h $(NVML_SRC) src/hud_drm_fdinfo.c src/hud_drm_fdinfo.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ \
		tests/benchmark_hud_metrics_cache.c src/hud_metrics_cache.c \
		src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c -ldl -pthread
build/test_pacer_queue: tests/test_pacer_queue.c src/pacer_queue.c src/pacer_queue.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_pacer_queue.c src/pacer_queue.c
build/test_pacer_compatibility: tests/test_pacer_compatibility.c src/pacer_compatibility.c src/pacer_compatibility.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_pacer_compatibility.c src/pacer_compatibility.c
build/test_hud_fps: tests/test_hud_fps.c src/hud_fps.c src/hud_fps.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_fps.c src/hud_fps.c -pthread
build/test-nvml.so: tests/nvml_provider.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -shared -o $@ $<
build/test-nvml-incomplete.so: tests/nvml_provider.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST_NVML_INCOMPLETE -shared -o $@ $<
build/frame-pacer-nvml-helper-test: src/hud_nvml_helper.c src/hud_nvml_protocol.c src/hud_nvml_protocol.h src/hud_nvml_provider.c src/hud_nvml_provider.h
	mkdir -p $(@D)
	$(CC) -m64 $(HELPER_CFLAGS) -DFRAME_PACER_TEST -Isrc -Wl,-s -pie -o $@ \
		src/hud_nvml_helper.c src/hud_nvml_protocol.c src/hud_nvml_provider.c -ldl
build/hud_nvml_helper_test_image.h: build/frame-pacer-nvml-helper-test Makefile
	{ echo '#ifndef FRAME_PACER_HUD_NVML_HELPER_TEST_IMAGE_H'; \
	  echo '#define FRAME_PACER_HUD_NVML_HELPER_TEST_IMAGE_H'; \
	  echo 'static const unsigned char frame_pacer_nvml_helper_image[] = {'; \
	  xxd -i < $<; \
	  echo '};'; \
	  echo 'static const unsigned int frame_pacer_nvml_helper_image_len = sizeof(frame_pacer_nvml_helper_image);'; \
	  echo '#endif'; } > $@
build/test_hud_nvml_protocol: tests/test_hud_nvml_protocol.c src/hud_nvml_protocol.c src/hud_nvml_protocol.h src/hud_nvml_provider.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_nvml_protocol.c src/hud_nvml_protocol.c
build/test-hud-nvml-client-i386: tests/test_hud_nvml_client.c src/hud_nvml_client.c src/hud_nvml_client.h src/hud_nvml_protocol.c src/hud_nvml_protocol.h build/hud_nvml_helper_test_image.h build/test-nvml.so
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -DFRAME_PACER_TEST \
		-DFRAME_PACER_TEST_REJECT_FD='"77"' \
		-DFRAME_PACER_NVML_HELPER_IMAGE_HEADER='"hud_nvml_helper_test_image.h"' \
		-DFRAME_PACER_TEST_NVML_LIBRARY='"$(abspath build/test-nvml.so)"' \
		-Isrc -o $@ tests/test_hud_nvml_client.c \
		src/hud_nvml_client.c src/hud_nvml_protocol.c -pthread
build/test-hud-nvml-client-retry-i386: tests/test_hud_nvml_client.c src/hud_nvml_client.c src/hud_nvml_client.h src/hud_nvml_protocol.c src/hud_nvml_protocol.h build/hud_nvml_helper_test_image.h
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -DFRAME_PACER_TEST \
		-DFRAME_PACER_TEST_EXPECT_FAILURE \
		-DFRAME_PACER_NVML_RETRY_ONE_NS=UINT64_C\(10000000\) \
		-DFRAME_PACER_NVML_RETRY_TWO_NS=UINT64_C\(30000000\) \
		-DFRAME_PACER_NVML_HELPER_IMAGE_HEADER='"hud_nvml_helper_test_image.h"' \
		-DFRAME_PACER_TEST_NVML_LIBRARY='"/frame-pacer/missing-nvml.so"' \
		-Isrc -o $@ tests/test_hud_nvml_client.c \
		src/hud_nvml_client.c src/hud_nvml_protocol.c -pthread
build/test-hud-nvml-client-target-exit-i386: tests/test_hud_nvml_client.c src/hud_nvml_client.c src/hud_nvml_client.h src/hud_nvml_protocol.c src/hud_nvml_protocol.h build/hud_nvml_helper_test_image.h build/test-nvml.so
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -DFRAME_PACER_TEST \
		-DFRAME_PACER_TEST_TARGET_EXIT \
		-DFRAME_PACER_NVML_RETRY_ONE_NS=UINT64_C\(10000000\) \
		-DFRAME_PACER_NVML_RETRY_TWO_NS=UINT64_C\(30000000\) \
		-DFRAME_PACER_NVML_HELPER_IMAGE_HEADER='"hud_nvml_helper_test_image.h"' \
		-DFRAME_PACER_TEST_NVML_LIBRARY='"$(abspath build/test-nvml.so)"' \
		-Isrc -o $@ tests/test_hud_nvml_client.c \
		src/hud_nvml_client.c src/hud_nvml_protocol.c -pthread
build/test-hud-metrics-external-i386: tests/test_hud_metrics_external.c src/hud_metrics.c src/hud_metrics.h $(NVML_SRC) src/hud_drm_fdinfo.c build/hud_nvml_helper_test_image.h build/test-nvml.so
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -DFRAME_PACER_TEST \
		-DFRAME_PACER_NVML_HELPER_IMAGE_HEADER='"hud_nvml_helper_test_image.h"' \
		-DFRAME_PACER_TEST_NVML_LIBRARY='"$(abspath build/test-nvml.so)"' \
		-Isrc -o $@ tests/test_hud_metrics_external.c \
		src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c -ldl -pthread
build/hud-nvml-helper-probe-i386: tests/hud_nvml_helper_probe.c src/hud_nvml_client.c src/hud_nvml_client.h src/hud_nvml_protocol.c src/hud_nvml_protocol.h build/hud_nvml_helper_image.h
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -Isrc -o $@ \
		tests/hud_nvml_helper_probe.c src/hud_nvml_client.c \
		src/hud_nvml_protocol.c -pthread
run-nvml-helper-probe: build/hud-nvml-helper-probe-i386
	sh ./tests/test_hud_nvml_helper.sh
build/test_hud_metrics: tests/test_hud_metrics.c src/hud_metrics.c src/hud_metrics.h $(NVML_SRC) src/hud_drm_fdinfo.c src/hud_drm_fdinfo.h build/test-nvml.so build/test-nvml-incomplete.so
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ tests/test_hud_metrics.c src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c -ldl -pthread
build/test_hud_metrics_cache: tests/test_hud_metrics_cache.c src/hud_metrics_cache.c src/hud_metrics_cache.h src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ tests/test_hud_metrics_cache.c src/hud_metrics_cache.c src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c -ldl -pthread
build/test_hud_text: tests/test_hud_text.c src/hud_text.c src/hud_text.h src/hud_metrics.h src/pacer_limit.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_text.c src/hud_text.c
build/test_thread_cpu_quota: tests/test_thread_cpu_quota.c src/thread_cpu_quota.c src/thread_cpu_quota.h $(THREAD_CPU_EXTERNAL_SRC) src/thread_cpu_external.h src/thread_cpu_protocol.c src/thread_cpu_protocol.h $(THREAD_CPU_SYSTEMD_SRC) src/thread_cpu_systemd.h $(STATE_DIRECTORY_SRC) src/state_directory.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ tests/test_thread_cpu_quota.c src/thread_cpu_quota.c $(THREAD_CPU_EXTERNAL_SRC) src/thread_cpu_protocol.c $(THREAD_CPU_SYSTEMD_SRC) $(STATE_DIRECTORY_SRC) -ldl -pthread
build/test_thread_cpu_protocol: tests/test_thread_cpu_protocol.c src/thread_cpu_protocol.c src/thread_cpu_protocol.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_thread_cpu_protocol.c src/thread_cpu_protocol.c
build/test_state_directory: tests/test_state_directory.c $(STATE_DIRECTORY_SRC) src/state_directory.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_state_directory.c $(STATE_DIRECTORY_SRC)
build/test_hud_font: tests/test_hud_font.c src/hud_font.c src/hud_font.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_font.c src/hud_font.c
build/test_hud_vertices: tests/test_hud_vertices.c src/hud_vertices.c src/hud_vertices.h src/hud_font.c src/hud_font.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vertices.c src/hud_vertices.c src/hud_font.c
build/test_hud_vulkan_resources: tests/test_hud_vulkan_resources.c src/hud_vulkan_resources.c src/hud_vulkan_resources.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_resources.c src/hud_vulkan_resources.c
build/test_hud_swapchain_policy: tests/test_hud_swapchain_policy.c src/hud_swapchain_policy.c src/hud_swapchain_policy.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_swapchain_policy.c src/hud_swapchain_policy.c
build/test_hud_vulkan_commands: tests/test_hud_vulkan_commands.c src/hud_vulkan_commands.c src/hud_vulkan_commands.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_commands.c src/hud_vulkan_commands.c
build/test_hud_vulkan_device: tests/test_hud_vulkan_device.c src/hud_vulkan_device.c src/hud_vulkan_device.h src/hud_vulkan_commands.c src/hud_metrics_cache.c src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_device.c src/hud_vulkan_device.c src/hud_vulkan_commands.c src/hud_metrics_cache.c src/hud_metrics.c $(NVML_SRC) src/hud_drm_fdinfo.c -ldl -pthread
build/test_hud_vulkan_draw_resources: tests/test_hud_vulkan_draw_resources.c src/hud_vulkan_draw_resources.c src/hud_vulkan_draw_resources.h src/hud_vulkan_resources.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_draw_resources.c src/hud_vulkan_draw_resources.c
build/test_hud_vulkan_pipeline: tests/test_hud_vulkan_pipeline.c src/hud_vulkan_pipeline.c src/hud_vulkan_pipeline.h src/hud_vertices.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_pipeline.c src/hud_vulkan_pipeline.c
build/test_hud_vulkan_vertex_buffer: tests/test_hud_vulkan_vertex_buffer.c src/hud_vulkan_vertex_buffer.c src/hud_vulkan_vertex_buffer.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_vertex_buffer.c src/hud_vulkan_vertex_buffer.c
build/test_hud_vulkan_record: tests/test_hud_vulkan_record.c src/hud_vulkan_record.c src/hud_vulkan_record.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_record.c src/hud_vulkan_record.c
build/test_hud_vulkan_present: tests/test_hud_vulkan_present.c src/hud_vulkan_present.c src/hud_vulkan_present.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/test_hud_vulkan_present.c src/hud_vulkan_present.c
build/test_frame_pacer_layer: tests/test_frame_pacer_layer.c $(VULKAN_SRC) $(HDRS) build/hud_spv.h $(VERSION_HEADER)
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -DFRAME_PACER_TEST -Isrc -o $@ tests/test_frame_pacer_layer.c $(VULKAN_SRC) -ldl -pthread
metrics-probe: build/hud-metrics-probe
build/hud-metrics-probe: tests/hud_metrics_probe.c src/hud_drm_fdinfo.c src/hud_drm_fdinfo.h src/hud_metrics.c src/hud_metrics.h $(NVML_SRC)
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ tests/hud_metrics_probe.c src/hud_drm_fdinfo.c src/hud_metrics.c $(NVML_SRC) -ldl -pthread
pci-probe: build/pci-bus-probe
build/pci-bus-probe: tests/pci_bus_probe.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ $< -lvulkan
hud-shaders: build/shaders/hud.vert.spv build/shaders/hud.frag.spv
build/shaders/hud.vert.spv: src/hud.vert
	mkdir -p $(@D)
	glslangValidator -V --target-env vulkan1.0 -S vert -o $@ $<
build/shaders/hud.frag.spv: src/hud.frag
	mkdir -p $(@D)
	glslangValidator -V --target-env vulkan1.0 -S frag -o $@ $<
build/hud_spv.h: build/shaders/hud.vert.spv build/shaders/hud.frag.spv
	mkdir -p $(@D)
	{ echo '#ifndef FRAME_PACER_HUD_SPV_H'; echo '#define FRAME_PACER_HUD_SPV_H'; xxd -i build/shaders/hud.vert.spv | sed -e 's/^unsigned char /static const unsigned char /' -e 's/\[\] =/[] __attribute__((aligned(4))) =/'; xxd -i build/shaders/hud.frag.spv | sed -e 's/^unsigned char /static const unsigned char /' -e 's/\[\] =/[] __attribute__((aligned(4))) =/'; echo '#endif'; } > $@
build/smoke: tests/smoke.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ tests/smoke.c -lvulkan
build/smoke-i386: tests/smoke.c
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -o $@ tests/smoke.c -lvulkan
build/smoke-device: tests/smoke_device.c
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -o $@ tests/smoke_device.c -lvulkan
build/smoke-device-i386: tests/smoke_device.c
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -o $@ tests/smoke_device.c -lvulkan
build/vulkan-present-probe: tests/vulkan_present_probe.c src/hud_vertices.h src/hud_text.h src/hud_font.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ $< -lvulkan -l:libX11.so.6
build/vulkan-present-probe-i386: tests/vulkan_present_probe.c src/hud_vertices.h src/hud_text.h src/hud_font.h
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -Isrc -o $@ $< -lvulkan -l:libX11.so.6
vulkan-present-probe: build/vulkan-present-probe build/vulkan-present-probe-i386
run-vulkan-present-probe: vulkan-present-probe $(VULKAN_ARTIFACTS)
	sh ./tests/test_vulkan_present.sh
build/glx-present-probe: tests/glx_present_probe.c src/hud_vertices.h src/hud_text.h src/hud_font.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ $< -l:libGL.so.1 -l:libX11.so.6
build/glx-present-probe-i386: tests/glx_present_probe.c src/hud_vertices.h src/hud_text.h src/hud_font.h
	mkdir -p $(@D)
	$(CC) -m32 $(BUILD_CFLAGS) -Isrc -o $@ $< -l:libGL.so.1 -l:libX11.so.6
glx-present-probe: build/glx-present-probe build/glx-present-probe-i386
run-glx-present-probe: glx-present-probe $(GL_ARTIFACTS) $(GL_RUNTIME_ARTIFACTS)
	sh ./tests/test_glx_present.sh
build/egl-present-probe: tests/egl_present_probe.c src/hud_vertices.h src/hud_text.h src/hud_font.h
	mkdir -p $(@D)
	$(CC) $(BUILD_CFLAGS) -Isrc -o $@ $< -l:libEGL.so.1 -l:libGL.so.1 -l:libX11.so.6
egl-present-probe: build/egl-present-probe
run-egl-present-probe: egl-present-probe $(GL_ARTIFACTS) $(GL_RUNTIME_ARTIFACTS)
	sh ./tests/test_egl_present.sh
UNIT_TESTS := \
	build/test_pacer_clock \
	build/test_pacer_limit \
	build/test_effective_config_report \
	build/test_thread_cpu_quota \
	build/test_thread_cpu_protocol \
	build/test_state_directory \
	build/test_log_retention \
	build/test_pacer_queue \
	build/test_pacer_compatibility \
	build/test_hud_fps \
	build/test_hud_nvml_protocol \
	build/test_hud_metrics \
	build/test_hud_metrics_cache \
	build/test_hud_text \
	build/test_hud_font \
	build/test_hud_vertices \
	build/test_hud_vulkan_resources \
	build/test_hud_swapchain_policy \
	build/test_hud_vulkan_commands \
	build/test_hud_vulkan_device \
	build/test_hud_vulkan_draw_resources \
	build/test_hud_vulkan_pipeline \
	build/test_hud_vulkan_vertex_buffer \
	build/test_hud_vulkan_record \
	build/test_hud_vulkan_present \
	build/test_frame_pacer_layer
# Unit recipes compile source files directly instead of through dependency
# objects. Rebuild them after any production-header edit so transitive includes
# cannot leave an apparently current but stale fixture in build/.
$(UNIT_TESTS): $(HDRS) build/hud_nvml_helper_image.h $(VERSION_HEADER)
SHELL_TESTS := $(wildcard tests/*.sh) $(wildcard packaging/*.sh) \
	$(wildcard .github/scripts/*.sh)

check-unit: $(UNIT_TESTS)
	@set -e; for test in $(UNIT_TESTS); do ./$$test; done
check-unit-i386:
	@set -e; $(MAKE) clean; trap '$(MAKE) clean' EXIT HUP INT TERM; \
		$(MAKE) check-unit CC='$(CC) -m32'; \
		file $(UNIT_TESTS) | grep -vq 'ELF 64-bit'
check-shell:
	@set -e; for test in $(SHELL_TESTS); do sh -n $$test; done
check-docs:
	sh ./tests/check_markdown_links.sh
check-workflows:
	python3 ./tests/check_workflows.py
	sh ./tests/test_release_notes.sh
check-hot-path-stack:
	CC="$(CC)" sh ./tests/check_hot_path_stack.sh

benchmark-performance: build/benchmark-pacer-limit build/benchmark-gl-present \
	build/benchmark-hud-metrics-cache build/x86_64/libframe_pacer_gl.so
	sh ./tests/benchmark_performance.sh

check: all hud-shaders $(GL_ARTIFACTS) $(GL_RUNTIME_ARTIFACTS) \
	build/test-gl-pacer \
	build/test-gl-pacer-i386 \
	build/test-gl-shim-noop \
	build/test-gl-shim-noop-i386 \
	build/smoke build/smoke-i386 build/smoke-device build/smoke-device-i386 \
	build/test-hud-nvml-client-i386 \
	build/test-hud-nvml-client-retry-i386 \
	build/test-hud-nvml-client-target-exit-i386 \
	build/test-hud-metrics-external-i386 \
	build/hud-nvml-helper-probe-i386 \
	check-unit check-shell check-docs check-workflows check-hud-image \
	check-hot-path-stack check-abi
	./build/test-hud-nvml-client-i386
	./build/test-hud-nvml-client-retry-i386
	./build/test-hud-nvml-client-target-exit-i386
	./build/test-hud-metrics-external-i386
	sh ./tests/test_hud_nvml_helper.sh
	sh ./tests/test_nvml_helper_embedding.sh
	sh ./tests/test_install.sh
	sh ./tests/test_version.sh
	sh ./tests/test_gl_pacer.sh
	sh ./tests/test_gl_pacer.sh i386
	sh ./tests/test_vulkan_layer.sh
	sh ./tests/test_vulkan_layer.sh i386
	sh ./tests/test_thread_cpu_quota_controller.sh
	spirv-val --target-env vulkan1.0 build/shaders/hud.vert.spv
	spirv-val --target-env vulkan1.0 build/shaders/hud.frag.spv
	file build/x86_64/libVkLayer_frame_pacer.so build/i386/libVkLayer_frame_pacer.so
	jq -e '.file_format_version == "1.0.0" and .layer.name == "VK_LAYER_ENDJYNN_frame_pacer" and .layer.type == "GLOBAL"' build/x86_64/layer/VkLayer_frame_pacer.json build/i386/layer/VkLayer_frame_pacer.json
	jq -e '.layer.enable_environment == {"ENABLE_FRAME_PACER": "1"} and .layer.disable_environment == {"DISABLE_FRAME_PACER": "1"} and .layer.functions.vkNegotiateLoaderLayerInterfaceVersion' build/x86_64/implicit_layer/VkLayer_frame_pacer.x86_64.json build/i386/implicit_layer/VkLayer_frame_pacer.i386.json
	jq -e '.layer.name == "VK_LAYER_ENDJYNN_frame_pacer_x86_64"' build/x86_64/implicit_layer/VkLayer_frame_pacer.x86_64.json
	jq -e '.layer.name == "VK_LAYER_ENDJYNN_frame_pacer_i386"' build/i386/implicit_layer/VkLayer_frame_pacer.i386.json
check-abi: $(VULKAN_ARTIFACTS) $(GL_ARTIFACTS)
	sh ./tests/check_abi.sh
check-analyzer:
	@set -e; $(MAKE) clean; trap '$(MAKE) clean' EXIT HUP INT TERM; \
		$(MAKE) check vulkan-present-probe glx-present-probe egl-present-probe \
			CFLAGS='-O0 -g -fanalyzer'
check-sanitize:
	@set -e; $(MAKE) clean; trap '$(MAKE) clean' EXIT HUP INT TERM; \
		$(MAKE) check-unit CFLAGS='-O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer'
check-tsan:
	@set -e; $(MAKE) clean; trap '$(MAKE) clean' EXIT HUP INT TERM; \
		$(MAKE) check-unit CFLAGS='-O1 -g -fsanitize=thread -fno-omit-frame-pointer'
check-coverage:
	@set -e; $(MAKE) clean; trap '$(MAKE) clean' EXIT HUP INT TERM; \
		$(MAKE) check CFLAGS='-O0 -g --coverage'; \
		$(MAKE) vulkan-present-probe CFLAGS='-O0 -g --coverage'; \
		sh ./tests/test_vulkan_present.sh || test $$? -eq 77; \
		$(MAKE) glx-present-probe CFLAGS='-O0 -g --coverage'; \
		sh ./tests/test_glx_present.sh || test $$? -eq 77; \
		$(MAKE) egl-present-probe CFLAGS='-O0 -g --coverage'; \
		sh ./tests/test_egl_present.sh || test $$? -eq 77; \
		$(MAKE) run-thread-cpu-quota-controller-integration CFLAGS='-O0 -g --coverage'; \
		sh ./tests/report_coverage.sh
clean:
	rm -rf build
