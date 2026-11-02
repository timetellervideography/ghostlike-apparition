cmake_minimum_required(VERSION 3.22)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
project(GhostlikeApparition VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)

# Version of the content pack (UI + engine + amp data) that ships inside the installer as the factory pack.
set(GL_PACK_VERSION "1.0.0" CACHE STRING "content pack version")
# Where the update button looks for new versions (manifest.json of the latest release).
set(GL_UPDATE_URL "https://github.com/timetellervideography/ghostlike-apparition/releases/latest/download/manifest.json" CACHE STRING "")

add_subdirectory(third_party/JUCE)

# ---------------------------------------------------------------- hot-swappable DSP engine (lives in the content pack)
add_library(GhostlikeEngine SHARED engine/engine.cpp)
target_compile_definitions(GhostlikeEngine PRIVATE GL_ENGINE_VERSION="${GL_PACK_VERSION}")
target_compile_options(GhostlikeEngine PRIVATE -O3 -fvisibility=hidden)
set_target_properties(GhostlikeEngine PROPERTIES OUTPUT_NAME GhostlikeEngine)
# unique install name per version, so dyld never confuses an updated engine with an already loaded one
target_link_options(GhostlikeEngine PRIVATE "-Wl,-install_name,@rpath/libGhostlikeEngine.${GL_PACK_VERSION}.dylib")

# ---------------------------------------------------------------- plugin shell (installed once)
juce_add_plugin(GhostlikeApparition
    PRODUCT_NAME "Ghostlike Apparition"
    COMPANY_NAME "Ghostlike"
    BUNDLE_ID "de.ghostlike.apparition"
    PLUGIN_MANUFACTURER_CODE Ghst
    PLUGIN_CODE Gapp
    FORMATS AU VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    AU_MAIN_TYPE kAudioUnitType_Effect
    VST3_CATEGORIES Fx Distortion
    NEEDS_WEB_BROWSER TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(GhostlikeApparition PRIVATE shell/PackManager.cpp shell/PluginProcessor.cpp shell/PluginEditor.cpp)
target_include_directories(GhostlikeApparition PRIVATE engine shell)
target_compile_definitions(GhostlikeApparition PUBLIC
    JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_MODAL_LOOPS_PERMITTED=0
    GL_SHELL_VERSION="${PROJECT_VERSION}" GL_UPDATE_URL="${GL_UPDATE_URL}")
target_link_libraries(GhostlikeApparition PRIVATE
    juce::juce_audio_utils juce::juce_gui_extra juce::juce_cryptography
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# The factory pack (build/pack + engine dylib) is copied into each bundle by tools/build.sh, which also signs them.
foreach(fmt AU VST3 Standalone)
  add_dependencies(GhostlikeApparition_${fmt} GhostlikeEngine)
endforeach()

# ---------------------------------------------------------------- update pipeline test (not shipped)
juce_add_console_app(GLUpdateTest PRODUCT_NAME "gl_updatetest")
target_sources(GLUpdateTest PRIVATE tools/updatetest.cpp shell/PackManager.cpp)
target_include_directories(GLUpdateTest PRIVATE engine shell)
target_compile_definitions(GLUpdateTest PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 GL_SHELL_VERSION="${PROJECT_VERSION}" GL_UPDATE_URL="${GL_UPDATE_URL}")
target_link_libraries(GLUpdateTest PRIVATE juce::juce_core juce::juce_events juce::juce_cryptography juce::juce_recommended_config_flags)
set_target_properties(GLUpdateTest PROPERTIES EXCLUDE_FROM_ALL TRUE)
