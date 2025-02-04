// taken from https://raw.githubusercontent.com/SaturnSH2x2/Sonic-CD-11-3DS/master/RSDKv3/Audio.cpp

#include "RetroEngine.hpp"
#include <cmath>
#include <iostream>

int globalSFXCount = 0;
int stageSFXCount  = 0;

int masterVolume  = MAX_VOLUME;
int trackID       = -1;
int sfxVolume     = MAX_VOLUME;
int bgmVolume     = MAX_VOLUME;
bool audioEnabled = false;
bool globalSfxLoaded = false;

int nextChannelPos;
bool musicEnabled;
int musicStatus;
TrackInfo musicTracks[TRACK_COUNT];
SFXInfo sfxList[SFX_COUNT];

ChannelInfo sfxChannels[CHANNEL_COUNT];

MusicPlaybackInfo musInfo;

int trackBuffer = -1;
int readVorbisCallCounter = 0;

#if RETRO_USING_SDLMIXER

#define LOCK_AUDIO_DEVICE()   ;
#define UNLOCK_AUDIO_DEVICE() ;

byte* trackData[TRACK_COUNT];
SDL_RWops* trackRwops[TRACK_COUNT];
byte* sfxData[SFX_COUNT];
SDL_RWops* sfxRwops[SFX_COUNT];

#elif RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
SDL_AudioSpec audioDeviceFormat;

#if RETRO_USING_SDL2
SDL_AudioDeviceID audioDevice;
SDL_AudioStream *ogv_stream;
#endif

#define LOCK_AUDIO_DEVICE()   SDL_LockAudio();
#define UNLOCK_AUDIO_DEVICE() SDL_UnlockAudio();

#define AUDIO_FREQUENCY (44100)
#define AUDIO_FORMAT    (AUDIO_S16SYS) /**< Signed 16-bit samples */
#define AUDIO_SAMPLES   (0x800)
#define AUDIO_CHANNELS  (2)

#define ADJUST_VOLUME(s, v) (s = (s * v) / MAX_VOLUME)

#else
#define LOCK_AUDIO_DEVICE()   ;
#define UNLOCK_AUDIO_DEVICE() ;
#endif

#define MIX_BUFFER_SAMPLES (256)

int InitAudioPlayback()
{
    StopAllSfx(); //"init"

#if RETRO_PLATFORM == RETRO_3DS && RETRO_USING_SDL1_AUDIO
    SDL_Init(SDL_INIT_AUDIO);
#endif

#if RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
    SDL_AudioSpec want;
    want.freq     = AUDIO_FREQUENCY;
    want.format   = AUDIO_FORMAT;
    want.samples  = AUDIO_SAMPLES;
    want.channels = AUDIO_CHANNELS;
    want.callback = ProcessAudioPlayback;

#if RETRO_USING_SDL2
    if ((audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, &audioDeviceFormat, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE)) > 0) {
        audioEnabled = true;
        SDL_PauseAudioDevice(audioDevice, 0);
    }
    else {
        printLog("Unable to open audio device: %s", SDL_GetError());
        audioEnabled = false;
        return true; // no audio but game wont crash now
    }

    // Init video sound stuff
    // TODO: Unfortunately, we're assuming that video sound is stereo at 48000Hz.
    // This is true of every .ogv file in the game (the Steam version, at least),
    // but it would be nice to make this dynamic. Unfortunately, THEORAPLAY's API
    // makes this awkward.
    ogv_stream = SDL_NewAudioStream(AUDIO_F32SYS, 2, 48000, audioDeviceFormat.format, audioDeviceFormat.channels, audioDeviceFormat.freq);
    if (!ogv_stream) {
        printLog("Failed to create stream: %s", SDL_GetError());
        SDL_CloseAudioDevice(audioDevice);
        audioEnabled = false;
        return true; // no audio but game wont crash now
    }
#elif RETRO_USING_SDL1_AUDIO
    if (SDL_OpenAudio(&want, &audioDeviceFormat) == 0) {
        audioEnabled = true;
        SDL_PauseAudio(0);
    }
    else {
        printLog("Unable to open audio device: %s", SDL_GetError());
        audioEnabled = false;
        return true; // no audio but game wont crash now
    }
#endif // !RETRO_USING_SDL1
#endif

#if RETRO_PLATFORM == RETRO_3DS && !RETRO_USING_SDL1_AUDIO && !RETRO_USING_SDLMIXER
    bool rtn = _3ds_audioInit();
    if (!rtn) {
        return true;
    }
#elif RETRO_USING_SDLMIXER
   if (Mix_OpenAudio(AUDIO_FREQUENCY, AUDIO_FORMAT, 4, 1024) == -1) {
	printLog("Unable to init SDL mixer: %s\n", Mix_GetError());
	audioEnabled = false;
	return true;
    }

    audioEnabled = true;
#endif

    LoadGlobalSfx();

    return true;
}

void LoadGlobalSfx()
{
    FileInfo info;
    FileInfo infoStore;
    char strBuffer[0x100];
    byte fileBuffer = 0;
    int fileBuffer2 = 0;

    if (globalSfxLoaded)
	    return;

    if (LoadFile("Data/Game/Gameconfig.bin", &info)) {
        infoStore = info;

        FileRead(&fileBuffer, 1);
        FileRead(strBuffer, fileBuffer);
        strBuffer[fileBuffer] = 0;

        FileRead(&fileBuffer, 1);
        FileRead(&strBuffer, fileBuffer); // Load 'Data'
        strBuffer[fileBuffer] = 0;

        FileRead(&fileBuffer, 1);
        FileRead(strBuffer, fileBuffer);
        strBuffer[fileBuffer] = 0;

        // Read Obect Names
        byte objectCount = 0;
        FileRead(&objectCount, 1);
        for (byte o = 0; o < objectCount; ++o) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;
        }

        // Read Script Paths
        for (byte s = 0; s < objectCount; ++s) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;
        }

        byte varCnt = 0;
        FileRead(&varCnt, 1);
        for (byte v = 0; v < varCnt; ++v) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;

            // Read Variable Value
            FileRead(&fileBuffer2, 4);
        }

        // Read SFX
        FileRead(&fileBuffer, 1);
        globalSFXCount = fileBuffer;
        for (byte s = 0; s < globalSFXCount; ++s) {
            FileRead(&fileBuffer, 1);
            FileRead(strBuffer, fileBuffer);
            strBuffer[fileBuffer] = 0;

            GetFileInfo(&infoStore);
            LoadSfx(strBuffer, s);
            SetFileInfo(&infoStore);

#if RETRO_USE_MOD_LOADER
            SetSfxName(strBuffer, s, true);
#endif
        }

        CloseFile();
        
#if RETRO_USE_MOD_LOADER
        Engine.LoadXMLSoundFX();
#endif
    }

    // sfxDataPosStage = sfxDataPos;
    nextChannelPos = 0;
    for (int i = 0; i < CHANNEL_COUNT; ++i) sfxChannels[i].sfxID = -1;

    globalSfxLoaded = true;
}

#if RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
size_t readVorbis(void *mem, size_t size, size_t nmemb, void *ptr)
{
    // put some FLEX TAPE® on that audio read error
    readVorbisCallCounter++;
    if (readVorbisCallCounter > 100 && musicStatus == MUSIC_PLAYING) {
	    return 0;
    }

    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    return FileRead2(&info->fileInfo, mem, (int)(size * nmemb), true);
}
int seekVorbis(void *ptr, ogg_int64_t offset, int whence)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    switch (whence) {
        case SEEK_SET: whence = 0; break;
        case SEEK_CUR: whence = (int)GetFilePosition2(&info->fileInfo); break;
        case SEEK_END: whence = info->fileInfo.vFileSize; break;
        default: break;
    }
    SetFilePosition2(&info->fileInfo, (int)(whence + offset));
    return (int)(whence + offset) <= info->fileInfo.vFileSize;
}
long tellVorbis(void *ptr)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    return GetFilePosition2(&info->fileInfo);
}
int closeVorbis(void *ptr)
{
    MusicPlaybackInfo *info = (MusicPlaybackInfo *)ptr;
    return CloseFile2(&info->fileInfo);
}
#endif

void ProcessMusicStream(Sint32 *stream, size_t bytes_wanted)
{
    if (!musInfo.loaded) {
        return;
    }
    switch (musicStatus) {
        case MUSIC_READY:
        case MUSIC_PLAYING: {
#if RETRO_USING_SDL2
            while (SDL_AudioStreamAvailable(musInfo.stream) < bytes_wanted) {
                // We need more samples: get some
                long bytes_read = ov_read(&musInfo.vorbisFile, (char *)musInfo.buffer, sizeof(musInfo.buffer), 0, 2, 1, &musInfo.vorbBitstream);

                if (bytes_read == 0) {
                    // We've reached the end of the file
                    if (musInfo.trackLoop) {
                        ov_pcm_seek(&musInfo.vorbisFile, musInfo.loopPoint);
                        continue;
                    }
                    else {
                        musicStatus = MUSIC_STOPPED;
                        break;
                    }
                }

                if (SDL_AudioStreamPut(musInfo.stream, musInfo.buffer, bytes_read) == -1)
                    return;
            }

            // Now that we know there are enough samples, read them and mix them
            int bytes_done = SDL_AudioStreamGet(musInfo.stream, musInfo.buffer, bytes_wanted);
            if (bytes_done == -1) {
                return;
            }
            if (bytes_done != 0)
                ProcessAudioMixing(stream, musInfo.buffer, bytes_done / sizeof(Sint16), (bgmVolume * masterVolume) / MAX_VOLUME, 0);
#endif

#if RETRO_USING_SDL1_AUDIO
            size_t bytes_gotten = 0;
            byte *buffer        = (byte *)malloc(bytes_wanted);
            memset(buffer, 0, bytes_wanted);
            while (bytes_gotten < bytes_wanted) {
                // We need more samples: get some
#if RETRO_PLATFORM == RETRO_3DS
		readVorbisCallCounter = 0;
		long bytes_read = ov_read(&musInfo.vorbisFile, (char*)musInfo.buffer,
				sizeof(musInfo.buffer) > (bytes_wanted - bytes_gotten) ? 
					(bytes_wanted - bytes_gotten) : sizeof(musInfo.buffer),
				&musInfo.vorbBitstream);
#else
                long bytes_read =
                    ov_read(&musInfo.vorbisFile, (char *)musInfo.buffer,
                            sizeof(musInfo.buffer) > (bytes_wanted - bytes_gotten) ? (bytes_wanted - bytes_gotten) : sizeof(musInfo.buffer), 0, 2, 1,
                            &musInfo.vorbBitstream);
#endif

                if (bytes_read == 0) {
		    printLog("bruh you got no bytes\n");
                    // We've reached the end of the file
                    if (musInfo.trackLoop) {
                        ov_pcm_seek(&musInfo.vorbisFile, musInfo.loopPoint);
                        continue;
                    }
                    else {
                        musicStatus = MUSIC_STOPPED;
                        break;
                    }
                }

                if (bytes_read > 0) {
                    memcpy(buffer + bytes_gotten, musInfo.buffer, bytes_read);
                    bytes_gotten += bytes_read;
                }
                else {
                    printLog("Music read error: vorbis error: %d", bytes_read);
                }
            }

            if (bytes_gotten > 0) {
                SDL_AudioCVT convert;
                MEM_ZERO(convert);
                int cvtResult = SDL_BuildAudioCVT(&convert, musInfo.spec.format, musInfo.spec.channels, musInfo.spec.freq, audioDeviceFormat.format,
                                                  audioDeviceFormat.channels, audioDeviceFormat.freq);
                if (cvtResult == 0) {
                    if (convert.len_mult > 0) {
                        convert.buf = (byte *)malloc(bytes_gotten * convert.len_mult);
                        convert.len = bytes_gotten;
                        memcpy(convert.buf, buffer, bytes_gotten);
                        SDL_ConvertAudio(&convert);
                    }
                }

                if (cvtResult == 0)
                    ProcessAudioMixing(stream, (const Sint16 *)convert.buf, bytes_gotten / sizeof(Sint16), (bgmVolume * masterVolume) / MAX_VOLUME,
                                       0);

                if (convert.len > 0 && convert.buf)
                    free(convert.buf);
            }
            if (bytes_wanted > 0)
                free(buffer);
#endif
            break;
        } 
        case MUSIC_STOPPED:
        case MUSIC_PAUSED:
        case MUSIC_LOADING:
            // dont play
            break;
    }
}

void ProcessAudioPlayback(void *userdata, Uint8 *stream, int len)
{
    (void)userdata; // Unused

    if (!audioEnabled)
        return;

    if (musicStatus == MUSIC_LOADING) {
        if (trackBuffer < 0 || trackBuffer >= TRACK_COUNT) {
            StopMusic();
            return;
        }

        TrackInfo *trackPtr = &musicTracks[trackBuffer];

        if (!trackPtr->fileName[0]) {
            StopMusic();
            return;
        }

        if (musInfo.loaded)
            StopMusic();

        if (LoadFile(trackPtr->fileName, &musInfo.fileInfo)) {
            musInfo.trackLoop = trackPtr->trackLoop;
            musInfo.loopPoint = trackPtr->loopPoint;
            musInfo.loaded    = true;

            unsigned long long samples = 0;
            ov_callbacks callbacks;

#if RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
            callbacks.read_func  = readVorbis;
            callbacks.seek_func  = seekVorbis;
            callbacks.tell_func  = tellVorbis;
            callbacks.close_func = closeVorbis;
#endif

            int error = ov_open_callbacks(&musInfo, &musInfo.vorbisFile, NULL, 0, callbacks);
            if (error != 0) {
            }

            musInfo.vorbBitstream = -1;
            musInfo.vorbisFile.vi = ov_info(&musInfo.vorbisFile, -1);

#if RETRO_USING_SDL2
            musInfo.stream = SDL_NewAudioStream(AUDIO_S16, musInfo.vorbisFile.vi->channels, musInfo.vorbisFile.vi->rate, audioDeviceFormat.format,
                                                audioDeviceFormat.channels, audioDeviceFormat.freq);
            if (!musInfo.stream) {
                printLog("Failed to create stream: %s", SDL_GetError());
            }
#endif

#if RETRO_USING_SDL1_AUDIO
            musInfo.spec.format   = AUDIO_S16;
            musInfo.spec.channels = musInfo.vorbisFile.vi->channels;
            musInfo.spec.freq     = (int)musInfo.vorbisFile.vi->rate;
#endif

            musInfo.buffer = new Sint16[MIX_BUFFER_SAMPLES];

            musicStatus  = MUSIC_PLAYING;
            masterVolume = MAX_VOLUME;
            trackID      = trackBuffer;
            trackBuffer  = -1;
        }
    }

    Sint16 *output_buffer = (Sint16 *)stream;

    size_t samples_remaining = (size_t)len / sizeof(Sint16);
    while (samples_remaining != 0) {
        Sint32 mix_buffer[MIX_BUFFER_SAMPLES];
        memset(mix_buffer, 0, sizeof(mix_buffer));

        const size_t samples_to_do = (samples_remaining < MIX_BUFFER_SAMPLES) ? samples_remaining : MIX_BUFFER_SAMPLES;

        // Mix music
        ProcessMusicStream(mix_buffer, samples_to_do * sizeof(Sint16));

#if RETRO_USING_SDL2
        // Process music being played by a video
        if (videoPlaying) {
            // Fetch THEORAPLAY audio packets, and shove them into the SDL Audio Stream
            const size_t bytes_to_do = samples_to_do * sizeof(Sint16);

            const THEORAPLAY_AudioPacket *packet;

            while ((packet = THEORAPLAY_getAudio(videoDecoder)) != NULL) {
                SDL_AudioStreamPut(ogv_stream, packet->samples, packet->frames * sizeof(float) * 2); // 2 for stereo
                THEORAPLAY_freeAudio(packet);
            }

            Sint16 buffer[MIX_BUFFER_SAMPLES];

            // If we need more samples, assume we've reached the end of the file,
            // and flush the audio stream so we can get more. If we were wrong, and
            // there's still more file left, then there will be a gap in the audio. Sorry.
            if (SDL_AudioStreamAvailable(ogv_stream) < bytes_to_do)
                SDL_AudioStreamFlush(ogv_stream);

            // Fetch the converted audio data, which is ready for mixing.
            int get = SDL_AudioStreamGet(ogv_stream, buffer, bytes_to_do);

            // Mix the converted audio data into the final output
            if (get != -1)
	        ProcessAudioMixing(mix_buffer, buffer, get / sizeof(Sint16), MAX_VOLUME, 0);
        }
        else {
            SDL_AudioStreamClear(ogv_stream); // Prevent leftover audio from playing at the start of the next video
        }
#endif

#if RETRO_USING_SDL1_AUDIO
        // Process music being played by a video
        // TODO: SDL1.2 lacks SDL_AudioStream so until someone finds good way to replicate that, I'm gonna leave this commented out
        /*if (videoPlaying) {
            // Fetch THEORAPLAY audio packets
            const size_t bytes_to_do = samples_to_do * sizeof(Sint16);
            size_t bytes_done        = 0;

            byte *vid_buffer             = (byte *)malloc(bytes_to_do);
            memset(vid_buffer, 0, bytes_to_do);

            const THEORAPLAY_AudioPacket *packet;

            while ((packet = THEORAPLAY_getAudio(videoDecoder)) != NULL) {
                int data_size = packet->frames * sizeof(float) * 2;
                if (bytes_done < bytes_to_do) {
                    memcpy(vid_buffer + bytes_done, packet->samples, data_size >= bytes_to_do ? bytes_to_do : data_size); // 2 for stereo
                    bytes_done += data_size >= bytes_to_do ? bytes_to_do : data_size;
                }
                THEORAPLAY_freeAudio(packet);
            }

            Sint16 convBuffer[MIX_BUFFER_SAMPLES];

            // If we need more samples, assume we've reached the end of the file,
            // and flush the audio stream so we can get more. If we were wrong, and
            // there's still more file left, then there will be a gap in the audio. Sorry.
            if (bytes_done < bytes_to_do) {
                memset(vid_buffer, 0, bytes_to_do);
            }

            if (bytes_done > 0) {
                SDL_AudioCVT convert;
                MEM_ZERO(convert);
                int cvtResult =
                    SDL_BuildAudioCVT(&convert, AUDIO_S16SYS, 2, 48000, audioDeviceFormat.format, audioDeviceFormat.channels, audioDeviceFormat.freq);
                if (cvtResult == 0) {
                    if (convert.len_mult > 0) {
                        convert.buf = (byte *)malloc(bytes_done * convert.len_mult);
                        convert.len = bytes_done;
                        memcpy(convert.buf, vid_buffer, bytes_done);
                        SDL_ConvertAudio(&convert);
                    }
                }

                if (cvtResult == 0)
                    ProcessAudioMixing(mix_buffer, (const Sint16 *)convert.buf, bytes_done / sizeof(Sint16), MAX_VOLUME, 0);

                if (convert.len > 0 && convert.buf)
                    free(convert.buf);
            }
        }*/
#endif

        // Mix SFX
        for (byte i = 0; i < CHANNEL_COUNT; ++i) {
            ChannelInfo *sfx = &sfxChannels[i];
            if (sfx == NULL)
                continue;

            if (sfx->sfxID < 0)
                continue;

            if (sfx->samplePtr) {
                Sint16 buffer[MIX_BUFFER_SAMPLES];

                size_t samples_done = 0;
                while (samples_done != samples_to_do) {
                    size_t sampleLen = (sfx->sampleLength < samples_to_do - samples_done) ? sfx->sampleLength : samples_to_do - samples_done;
                    memcpy(&buffer[samples_done], sfx->samplePtr, sampleLen * sizeof(Sint16));

                    samples_done += sampleLen;
                    sfx->samplePtr += sampleLen;
                    sfx->sampleLength -= sampleLen;

                    if (sfx->sampleLength == 0) {
                        if (sfx->loopSFX) {
                            sfx->samplePtr    = sfxList[sfx->sfxID].buffer;
                            sfx->sampleLength = sfxList[sfx->sfxID].length;
                        }
                        else {
                            StopSfx(sfx->sfxID);
                            break;
                        }
                    }
                }

#if RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
                ProcessAudioMixing(mix_buffer, buffer, samples_done, sfxVolume, sfx->pan);
#endif
            }
        }

        // Clamp mixed samples back to 16-bit and write them to the output buffer
        for (size_t i = 0; i < sizeof(mix_buffer) / sizeof(*mix_buffer); ++i) {
            const Sint16 max_audioval = ((1 << (16 - 1)) - 1);
            const Sint16 min_audioval = -(1 << (16 - 1));

            const Sint32 sample = mix_buffer[i];

            if (sample > max_audioval)
                *output_buffer++ = max_audioval;
            else if (sample < min_audioval)
                *output_buffer++ = min_audioval;
            else
                *output_buffer++ = sample;
        }

        samples_remaining -= samples_to_do;
    }
}

#if RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
void ProcessAudioMixing(Sint32 *dst, const Sint16 *src, int len, int volume, sbyte pan)
{
    if (volume == 0)
        return;

    if (volume > MAX_VOLUME)
        volume = MAX_VOLUME;

    float panL = 0;
    float panR = 0;
    int i      = 0;

    if (pan < 0) {
        panR = 1.0f - abs(pan / 100.0f);
        panL = 1.0f;
    }
    else if (pan > 0) {
        panL = 1.0f - abs(pan / 100.0f);
        panR = 1.0f;
    }

    while (len--) {
        Sint32 sample = *src++;
        ADJUST_VOLUME(sample, volume);

        if (pan != 0) {
            if ((i % 2) != 0) {
                sample *= panR;
            }
            else {
                sample *= panL;
            }
        }

        *dst++ += sample;

        i++;
    }
}
#endif

#if RETRO_USE_MOD_LOADER
char globalSfxNames[SFX_COUNT][0x40];
char stageSfxNames[SFX_COUNT][0x40];
void SetSfxName(const char *sfxName, int sfxID, bool global)
{
    char *sfxNamePtr = global ? globalSfxNames[sfxID] : stageSfxNames[sfxID];

    int sfxNamePos = 0;
    int sfxPtrPos  = 0;
    byte mode      = 0;
    while (sfxName[sfxNamePos]) {
        if (sfxName[sfxNamePos] == '.' && mode == 1)
            mode = 2;
        else if ((sfxName[sfxNamePos] == '/' || sfxName[sfxNamePos] == '\\') && !mode)
            mode = 1;
        else if (sfxName[sfxNamePos] != ' ' && mode == 1)
            sfxNamePtr[sfxPtrPos++] = sfxName[sfxNamePos];
        ++sfxNamePos;
    }
    sfxNamePtr[sfxPtrPos] = 0;
    printLog("Set %s SFX (%d) name to: %s", (global ? "Global" : "Stage"), sfxID, sfxNamePtr);
}
#endif

void SetMusicTrack(char *filePath, byte trackID, bool loop, uint loopPoint)
{
#if RETRO_USING_SDLMIXER
    char tempbuf[0xff];
    StrCopy(tempbuf, "Data/Music/");
    StrCopy(tempbuf, filePath);
    if (StrComp(tempbuf, musicTracks[trackID].fileName)) {
        printLog("%s already loaded, ignoring", tempbuf);
	return;
    }
#endif

    printLog("SetMusicTrack: %s\n", filePath);
    LOCK_AUDIO_DEVICE()
    TrackInfo *track = &musicTracks[trackID];
    StrCopy(track->fileName, "Data/Music/");
    StrAdd(track->fileName, filePath);
    track->trackLoop = loop;
    track->loopPoint = loopPoint;
    musicStatus = MUSIC_LOADING;

#if RETRO_USING_SDLMIXER
    FileInfo info;
    char fullPath[0x80];

    StrCopy(fullPath, musicTracks[trackID].fileName);

    if (LoadFile(fullPath, &info)) {
        trackData[trackID] = (byte*) malloc(info.fileSize * sizeof(byte));
        FileRead(trackData[trackID], info.fileSize);
        CloseFile();

        trackRwops[trackID] = SDL_RWFromConstMem(trackData[trackID], info.fileSize);
        if (trackRwops[trackID] == NULL) {
	    printLog("Unable to open music: %s", info.fileName);
        } else {
           musicTracks[trackID].mus  = Mix_LoadMUS_RW(trackRwops[trackID]);
           if (!musicTracks[trackID].mus) {
	        printLog("Unable to read music: %s", info.fileName);
           }
        }
    }
#endif
    UNLOCK_AUDIO_DEVICE()
}

#if RETRO_USING_SDLMIXER
void MixHook() {
    if (musicTracks[trackID].trackLoop) {
        Mix_PlayMusic(musicTracks[trackID].mus, 0);
        Mix_SetMusicPosition(musicTracks[trackID].loopPoint / AUDIO_FREQUENCY * AUDIO_CHANNELS);
    } else {
	musicStatus = MUSIC_STOPPED;
    }
}
#endif

bool PlayMusic(int track)
{
    if (!audioEnabled)
        return false;

    printLog("PlayMusic: %d\n", track);
#if RETRO_USING_SDLMIXER


    Mix_HookMusicFinished(MixHook);
    Mix_VolumeMusic(128);
    Mix_PlayMusic(musicTracks[track].mus, 0);
    musicStatus = MUSIC_PLAYING;
    trackID = track;
    trackBuffer = -1;
    masterVolume = MAX_VOLUME;
#elif RETRO_USING_SDL2 || RETRO_USING_SDL1_AUDIO
    
    LOCK_AUDIO_DEVICE()
    if (track < 0 || track >= TRACK_COUNT) {
        StopMusic();
        trackBuffer = -1;
        return false;
    }
    trackBuffer = track;
    musicStatus = MUSIC_LOADING;
    UNLOCK_AUDIO_DEVICE()
#endif

#if RETRO_PLATFORM == RETRO_3DS && !RETRO_USING_SDL1_AUDIO
    LightEvent_Signal(&s_event);
#endif
    return true;
}

void LoadSfx(char *filePath, byte sfxID)
{
    if (!audioEnabled)
        return;

    FileInfo info;
    char fullPath[0x80];

    StrCopy(fullPath, "Data/SoundFX/");
    StrAdd(fullPath, filePath);

    if (LoadFile(fullPath, &info)) {
#if RETRO_USING_SDLMIXER
	sfxData[sfxID] = (byte*) malloc(info.fileSize * sizeof(byte));
	FileRead(sfxData[sfxID], info.fileSize);
	CloseFile();

        sfxRwops[sfxID] = SDL_RWFromConstMem(sfxData[sfxID], info.fileSize);
	if (sfxRwops[sfxID] == NULL) {
	    printLog("Unable to open sfx: %s", info.fileName);
	} else {
	    sfxList[sfxID].chunk = Mix_LoadWAV_RW(sfxRwops[sfxID], 1);
	    if (!sfxList[sfxID].chunk) {
		printLog("Unable to read sfx: %s", info.fileName);
	    } else {
		StrCopy(sfxList[sfxID].name, filePath);
		sfxList[sfxID].loaded = true;
	    }
	}

	free(sfxData[sfxID]);
#elif RETRO_USING_SDL1_AUDIO || RETRO_USING_SDL2
        byte *sfx = new byte[info.fileSize];
        FileRead(sfx, info.fileSize);
        CloseFile();

        SDL_LockAudio();
        SDL_RWops *src = SDL_RWFromConstMem(sfx, info.fileSize);
        if (src == NULL) {
            printLog("Unable to open sfx: %s", info.fileName);
        }
        else {
            SDL_AudioSpec wav_spec;
            uint wav_length;
            byte *wav_buffer;
#if RETRO_PLATFORM == RETRO_3DS
	    SDL_AudioSpec *wav = SDL_LoadWAV_RW(src, 0, &wav_spec, &wav_buffer, (u32*)&wav_length);
#else
            SDL_AudioSpec *wav = SDL_LoadWAV_RW(src, 0, &wav_spec, &wav_buffer, &wav_length);
#endif

            SDL_RWclose(src);
            delete[] sfx;
            if (wav == NULL) {
                printLog("Unable to read sfx: %s", info.fileName);
            }
            else {
                SDL_AudioCVT convert;
                if (SDL_BuildAudioCVT(&convert, wav->format, wav->channels, wav->freq, audioDeviceFormat.format, audioDeviceFormat.channels,
                                      audioDeviceFormat.freq)
                    > 0) {
                    convert.buf = (byte *)malloc(wav_length * convert.len_mult);
                    convert.len = wav_length;
                    memcpy(convert.buf, wav_buffer, wav_length);
                    SDL_ConvertAudio(&convert);

                    StrCopy(sfxList[sfxID].name, filePath);
                    sfxList[sfxID].buffer = (Sint16 *)convert.buf;
                    sfxList[sfxID].length = convert.len_cvt / sizeof(Sint16);
                    sfxList[sfxID].loaded = true;
                    SDL_FreeWAV(wav_buffer);
                }
                else {
                    StrCopy(sfxList[sfxID].name, filePath);
                    sfxList[sfxID].buffer = (Sint16 *)wav_buffer;
                    sfxList[sfxID].length = wav_length / sizeof(Sint16);
                    sfxList[sfxID].loaded = true;
                }
            }
        }
        SDL_UnlockAudio();
#elif RETRO_PLATFORM == RETRO_3DS
    //uint wav_length =   ((unsigned char)sfx[43] << 24) | 
    //	    		((unsigned char)sfx[42] << 16) | 
    //			((unsigned char)sfx[41] << 8)  | 
    //			(unsigned char)sfx[40];

    uint wav_length = info.fileSize - 44;
    sfxList[sfxID].buffer = (s16*) malloc(wav_length * CHANNELS_PER_SAMPLE);
    //memcpy(sfxList[sfxID].buffer, sfx + 44, wav_length);
	
    //convert unsigned 8-bit audio to signed 8-bit
    u8* in = (u8*)sfx + 44;
    u8* out = (u8*)sfxList[sfxID].buffer;
    for (unsigned long i = 0; i < wav_length; ++i)
    {
        *out = *in - 128;
        out++;
	*out = *in - 128;
	out++;
        in++;
    }
	
    StrCopy(sfxList[sfxID].name, filePath);
    sfxList[sfxID].length = wav_length / sizeof(s8);
    sfxList[sfxID].loaded = true;
    printf("Load: %s, %d samples\n", sfxList[sfxID].name, sfxList[sfxID].length);

    delete[] sfx;
#endif
    }
}
void PlaySfx(int sfx, bool loop)
{
    LOCK_AUDIO_DEVICE()
    int sfxChannelID = nextChannelPos++;
    for (int c = 0; c < CHANNEL_COUNT; ++c) {
        if (sfxChannels[c].sfxID == sfx) {
            sfxChannelID = c;
            break;
        }
    }

#if RETRO_USING_SDLMIXER
    Mix_PlayChannel(-1, sfxList[sfx].chunk, loop ? -1 : 0);
#elif RETRO_USING_SDL2 || RETRO_USING_SDL1_AUDIO
    ChannelInfo *sfxInfo  = &sfxChannels[sfxChannelID];
    sfxInfo->sfxID        = sfx;
    sfxInfo->samplePtr    = sfxList[sfx].buffer;
    sfxInfo->sampleLength = sfxList[sfx].length;
    sfxInfo->loopSFX      = loop;
    sfxInfo->pan          = 0;
    if (nextChannelPos == CHANNEL_COUNT)
        nextChannelPos = 0;
#if RETRO_PLATFORM == RETRO_3DS && !RETRO_USING_SDL1_AUDIO
    LightEvent_Signal(&s_event);
#endif
#endif
    UNLOCK_AUDIO_DEVICE()
}
void SetSfxAttributes(int sfx, int loopCount, sbyte pan)
{
    // this is commented out because of a very bizarre bug in Palmtree Panic Zone 3
    // where the game will lock up in a very specific spot
    //
    // I have no idea why this happens, but I figure I'll leave this code commented
    // out for the time being

    // we'll do this right eventually, but this is a hack
    // to get ring SFX to play, albeit without the stereo alternation
#if RETRO_USING_SDLMIXER
    u8 l = (pan < 0) ? abs(pan) * 2 : 0;
    u8 r = (pan > 0) ? pan * 2 : 0;
    Mix_SetPanning(1, l, r);
    Mix_PlayChannel(1, sfxList[sfx].chunk, 0);
#else
    LOCK_AUDIO_DEVICE()
    int sfxChannel = -1;
    for (int i = 0; i < CHANNEL_COUNT; ++i) {
        if (sfxChannels[i].sfxID == sfx || sfxChannels[i].sfxID == -1) {
            sfxChannel = i;
            break;
        }
    }
    if (sfxChannel == -1)
        return; // wasn't found

    // TODO: is this right? should it play an sfx here? without this rings dont play any sfx so I assume it must be?
    ChannelInfo *sfxInfo  = &sfxChannels[sfxChannel];
    sfxInfo->samplePtr    = sfxList[sfx].buffer;
    sfxInfo->sampleLength = sfxList[sfx].length;
    sfxInfo->loopSFX      = loopCount == -1 ? sfxInfo->loopSFX : loopCount;
    sfxInfo->pan          = pan;
    sfxInfo->sfxID        = sfx;
    UNLOCK_AUDIO_DEVICE()
    PlaySfx(sfx, false);
#endif
}

#if RETRO_USING_C2D
void ProcessMusicStream() {
    return;
}

void ProcessAudioPlayback() {
    return;
}

#endif