#define boolean windows_boolean_workaround
#include <windows.h>
#undef boolean

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ogg_temp.h"

int stb_vorbis_decode_memory(const unsigned char *mem, int len, int *channels,
    int *sample_rate, short **output);

static int create_temp_wav_path(char *path, size_t path_size)
{
    char temp_dir[MAX_PATH];
    char temp_file[MAX_PATH];

    if (!GetTempPathA(MAX_PATH, temp_dir))
        return 0;

    if (!GetTempFileNameA(temp_dir, "dmu", 0, temp_file))
        return 0;

    remove(temp_file);
    snprintf(path, path_size, "%s.wav", temp_file);
    return 1;
}

static int write_wav_file(const char *path, const short *pcm, int channels,
    int sample_rate, int frames)
{
    FILE *stream;
    unsigned int data_bytes;
    unsigned int riff_size;
    unsigned int byte_rate;
    unsigned short block_align;
    unsigned short bits_per_sample;
    unsigned short channel_count;

    if (!path || !pcm || channels <= 0 || sample_rate <= 0 || frames <= 0)
        return 0;

    data_bytes = (unsigned int)(frames * channels * (int)sizeof(short));
    riff_size = 36u + data_bytes;
    bits_per_sample = 16;
    channel_count = (unsigned short)channels;
    block_align = (unsigned short)(channels * (bits_per_sample / 8));
    byte_rate = (unsigned int)(sample_rate * block_align);

    stream = fopen(path, "wb");
    if (!stream)
        return 0;

    if (fwrite("RIFF", 1, 4, stream) != 4
        || fwrite(&riff_size, 4, 1, stream) != 1
        || fwrite("WAVE", 1, 4, stream) != 4
        || fwrite("fmt ", 1, 4, stream) != 4)
        goto fail;

    {
        unsigned int fmt_size = 16;
        unsigned short audio_format = 1;

        if (fwrite(&fmt_size, 4, 1, stream) != 1
            || fwrite(&audio_format, 2, 1, stream) != 1
            || fwrite(&channel_count, 2, 1, stream) != 1
            || fwrite(&sample_rate, 4, 1, stream) != 1
            || fwrite(&byte_rate, 4, 1, stream) != 1
            || fwrite(&block_align, 2, 1, stream) != 1
            || fwrite(&bits_per_sample, 2, 1, stream) != 1)
            goto fail;
    }

    if (fwrite("data", 1, 4, stream) != 4
        || fwrite(&data_bytes, 4, 1, stream) != 1
        || fwrite(pcm, 1, data_bytes, stream) != data_bytes)
        goto fail;

    fclose(stream);
    return 1;

fail:
    fclose(stream);
    remove(path);
    return 0;
}

int OGG_WriteTempWavFromMemory(const unsigned char *ogg_data, int ogg_len,
    char *out_path, size_t out_path_size)
{
    int channels;
    int sample_rate;
    int frames;
    short *pcm;

    channels = 0;
    sample_rate = 0;
    pcm = NULL;

    if (!ogg_data || ogg_len <= 0)
        return 0;

    if (!create_temp_wav_path(out_path, out_path_size))
        return 0;

    frames = stb_vorbis_decode_memory(ogg_data, ogg_len, &channels, &sample_rate, &pcm);
    if (frames <= 0 || !pcm)
        return 0;

    if (!write_wav_file(out_path, pcm, channels, sample_rate, frames))
    {
        free(pcm);
        return 0;
    }

    free(pcm);
    return 1;
}
