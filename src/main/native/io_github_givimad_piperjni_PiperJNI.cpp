#include <iostream>
#include <map>
#include <vector>
#include <string>
#include <mutex>
#include <random>
#include "io_github_givimad_piperjni_PiperJNI.h"
#include "piper.h"
#include "piper_impl.hpp"
#include <spdlog/spdlog.h>

// Helper to convert version macro to string
#define STR(x) #x
#define XSTR(x) STR(x)

std::map<int, piper_synthesizer*> voiceMap;
std::mutex voiceMapMutex;

// Disable library log
class StartUp
{
public:
   StartUp()
   {
    spdlog::set_level(spdlog::level::off);
   }
};
StartUp startup;

// Exception helper
// From https://stackoverflow.com/a/12014833/6189530
struct NewJavaException {
    NewJavaException(JNIEnv * env, const char* type="", const char* message="")
    {
        jclass newExcCls = env->FindClass(type);
        if (newExcCls != NULL)
            env->ThrowNew(newExcCls, message);
        // if it is null, a NoClassDefFoundError was already thrown
    }
};

void swallow_cpp_exception_and_throw_java(JNIEnv * env) {
    try {
        throw;
    } catch(const std::bad_alloc& rhs) {
        // translate OOM C++ exception to Java exception
        NewJavaException(env, "java/lang/OutOfMemoryError", rhs.what());
    } catch(const std::exception& e) {
        // translate IO C++ exception to Java exception
        NewJavaException(env, "java/lang/RuntimeException", e.what());
    } catch(...) {
        // translate unknown C++ exception to Java error
        NewJavaException(env, "java/lang/Error", "Unknown native exception type");
    }
}

// RAII Wrapper for JNI String
class JNIString {
    JNIEnv* env;
    jstring jstr;
    const char* cstr;

public:
    JNIString(JNIEnv* env, jstring jstr) : env(env), jstr(jstr), cstr(nullptr) {
        if (jstr) {
            cstr = env->GetStringUTFChars(jstr, NULL);
        }
    }

    ~JNIString() {
        if (cstr) {
            env->ReleaseStringUTFChars(jstr, cstr);
        }
    }

    const char* get() const {
        return cstr;
    }

    operator const char*() const {
        return cstr;
    }

    bool isValid() const {
        return !jstr || cstr != nullptr;
    }
};

int getVoiceId() {
    static thread_local std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(1, 2147483647); // Positive integers

    int i = 0;
    while (i++ < 1000) {
        int id = dist(rng);
        // Caller must hold voiceMapMutex
        if(!voiceMap.count(id)) {
            return id;
        }
    }
    throw std::runtime_error("Wrapper error: Unable to get voice id");
}

// JNI Implementations

JNIEXPORT jint JNICALL Java_io_github_givimad_piperjni_PiperJNI_loadVoice(JNIEnv *env, jobject thisObject, jstring espeakDataPath, jstring modelPath, jstring modelConfigPath, jlong jSpeakerId) {
    piper_synthesizer* voice = nullptr;
    try {
        JNIString cEspeakDataPath(env, espeakDataPath);
        JNIString cModelPath(env, modelPath);
        JNIString cModelConfigPath(env, modelConfigPath);

        if (!cModelPath.isValid() || !cModelConfigPath.isValid() || (espeakDataPath && !cEspeakDataPath.isValid())) {
             NewJavaException(env, "java/lang/OutOfMemoryError", "Failed to allocate string chars");
             return -1;
        }

        voice = piper_create(cModelPath, cModelConfigPath, cEspeakDataPath);

        if (!voice) {
             NewJavaException(env, "java/lang/RuntimeException", "Failed to load voice");
             return -1;
        }

        // Set speaker id if provided
        if (jSpeakerId > -1) {
             voice->speaker_id = (SpeakerId)jSpeakerId;
        }

        std::lock_guard<std::mutex> lock(voiceMapMutex);
        int ref = getVoiceId();
        voiceMap.insert({ref, voice});
        return ref;
    } catch (const std::exception&) {
        if (voice) {
            piper_free(voice);
        }
        swallow_cpp_exception_and_throw_java(env);
        return -1;
    }
}

JNIEXPORT jboolean JNICALL Java_io_github_givimad_piperjni_PiperJNI_voiceUsesESpeakPhonemes(JNIEnv *env, jobject thisObject, jint voiceRef) {
    try {
        return true;
    } catch (const std::exception&) {
        swallow_cpp_exception_and_throw_java(env);
        return false;
    }
}

JNIEXPORT jint JNICALL Java_io_github_givimad_piperjni_PiperJNI_voiceSampleRate(JNIEnv *env, jobject thisObject, jint voiceRef) {
    try {
        std::lock_guard<std::mutex> lock(voiceMapMutex);
        return (jint) voiceMap.at(voiceRef)->sample_rate;
    } catch (const std::exception&) {
        swallow_cpp_exception_and_throw_java(env);
        return 0;
    }
}

JNIEXPORT void JNICALL Java_io_github_givimad_piperjni_PiperJNI_freeVoice(JNIEnv *env, jobject thisObject, jint voiceRef) {
    std::lock_guard<std::mutex> lock(voiceMapMutex);
    if (voiceMap.count(voiceRef)) {
        piper_free(voiceMap.at(voiceRef));
        voiceMap.erase(voiceRef);
    }
}

JNIEXPORT jshortArray JNICALL Java_io_github_givimad_piperjni_PiperJNI_textToAudio(JNIEnv *env, jobject thisObject, jint voiceRef, jstring jText, jobject jAudioCallback) {
    try {
        piper_synthesizer* voice = nullptr;
        {
            std::lock_guard<std::mutex> lock(voiceMapMutex);
            voice = voiceMap.at(voiceRef);
        }

        JNIString cppText(env, jText);
        if (!cppText.isValid()) {
             NewJavaException(env, "java/lang/OutOfMemoryError", "Failed to allocate string chars");
             return NULL;
        }

        piper_synthesize_options options = piper_default_synthesize_options(voice);

        if (piper_synthesize_start(voice, cppText.get(), &options) != PIPER_OK) {
             NewJavaException(env, "java/lang/RuntimeException", "Failed to start synthesis");
             return NULL;
        }

        std::vector<int16_t> fullAudioBuffer;
        piper_audio_chunk chunk;
        int ret;

        jclass cbClass = NULL;
        jmethodID cbMethodId = NULL;
        if (jAudioCallback) {
             cbClass = env->GetObjectClass(jAudioCallback);
             cbMethodId = env->GetMethodID(cbClass, "onAudio", "([S)V");
             if (!cbMethodId) return NULL;
        }

        while ((ret = piper_synthesize_next(voice, &chunk)) != PIPER_DONE) {
             if (ret != PIPER_OK) {
                  // Error
                  break;
             }
             if (chunk.num_samples > 0) {
                  std::vector<int16_t> chunkSamples;
                  // Convert float samples to int16
                  chunkSamples.reserve(chunk.num_samples);
                  for (size_t i = 0; i < chunk.num_samples; ++i) {
                      float val = chunk.samples[i];
                      // clip
                      if (val > 1.0f) val = 1.0f;
                      if (val < -1.0f) val = -1.0f;
                      chunkSamples.push_back((int16_t)(val * 32767.0f));
                  }

                  if (jAudioCallback) {
                        jshortArray jAudioBuffer = env->NewShortArray(chunkSamples.size());
                        if (!jAudioBuffer) {
                             NewJavaException(env, "java/lang/OutOfMemoryError", "Failed to allocate audio buffer");
                             return NULL;
                        }

                        jshort *jSamples = env->GetShortArrayElements(jAudioBuffer, NULL);
                        if (!jSamples) {
                             NewJavaException(env, "java/lang/OutOfMemoryError", "Failed to get array elements");
                             return NULL;
                        }

                        for (size_t index = 0; index < chunkSamples.size(); ++index) {
                            jSamples[index]= (jshort) chunkSamples[index];
                        }
                        env->ReleaseShortArrayElements(jAudioBuffer, jSamples, 0);
                        env->CallVoidMethod(jAudioCallback, cbMethodId, jAudioBuffer);
                        env->DeleteLocalRef(jAudioBuffer);

                        if (env->ExceptionCheck()) {
                             return NULL;
                        }
                  } else {
                        fullAudioBuffer.insert(fullAudioBuffer.end(), chunkSamples.begin(), chunkSamples.end());
                  }
             }
        }

        if (!jAudioCallback) {
            jshortArray jAudioBuffer = env->NewShortArray(fullAudioBuffer.size());
            if (!jAudioBuffer) {
                 NewJavaException(env, "java/lang/OutOfMemoryError", "Failed to allocate full audio buffer");
                 return NULL;
            }
            jshort *jSamples = env->GetShortArrayElements(jAudioBuffer, NULL);
            if (!jSamples) {
                 NewJavaException(env, "java/lang/OutOfMemoryError", "Failed to get array elements");
                 return NULL;
            }
            for (size_t index = 0; index < fullAudioBuffer.size(); ++index) {
                jSamples[index]= (jshort) fullAudioBuffer[index];
            }
            env->ReleaseShortArrayElements(jAudioBuffer, jSamples, 0);
            return jAudioBuffer;
        }

        return NULL;

    } catch (const std::exception&) {
           swallow_cpp_exception_and_throw_java(env);
           return NULL;
    }
}

JNIEXPORT jstring JNICALL Java_io_github_givimad_piperjni_PiperJNI_getVersion(JNIEnv *env, jobject thisObject) {
    return env->NewStringUTF(XSTR(_PIPER_VERSION));
}
