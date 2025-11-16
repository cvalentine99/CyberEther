if catch2_dep.found()
    subdir('memory')
endif

test('memory', executable(
    'jetstream-memory', 'memory.cc',
    dependencies: libjetstream_dep,
), is_parallel: false, timeout: 0)

test('backend-telemetry', executable(
    'jetstream-backend-telemetry', 'backend_telemetry.cc',
    dependencies: libjetstream_dep,
), is_parallel: false, timeout: 0)