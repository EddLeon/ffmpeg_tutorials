/*
 * Copyright (c) 2003 Fabrice Bellard
 *						  2016 Eduardo De Leon	
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/**
 * @file
 * libavformat API example.
 *
 * Output a media file in any supported libavformat format. The default
 * codecs are used.
 * @example muxing.c
 */
 
 /**
 * Compile with:
 *  g++ -Wall mux_audio.cpp -o mux_audio $(pkg-config --cflags --libs ao libavformat libavcodec libswresample libswscale libavutil)
**/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <iostream>
extern "C"
{
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include "libavutil/audio_fifo.h"
#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavutil/timestamp.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <ao/ao.h>
}
#define STREAM_DURATION   10.0
#define STREAM_FRAME_RATE 25 /* 25 images/s */
#define STREAM_PIX_FMT    AV_PIX_FMT_YUV420P /* default pix_fmt */

#define SCALE_FLAGS SWS_BICUBIC
ao_device *adevice;
ao_sample_format sformat;
using namespace std;

// a wrapper around a single output AVStream
typedef struct OutputStream {
    AVStream *st;
    AVCodecContext *enc;

    /* pts of the next frame that will be generated */
    int64_t next_pts;
    int samples_count;

    AVFrame *frame;
    AVFrame *tmp_frame;

    float t, tincr, tincr2;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
} OutputStream;

typedef struct InputStream{
	AVFrame* frame;
	AVFormatContext * formatContext;
	AVCodec* cdc;
	AVCodecID codec_id;
	AVCodecContext* codecContext;
	AVStream* audioStream;
	char* filename;
}InputStream;

/** Initialize a FIFO buffer for the audio samples to be encoded. */
static int init_fifo(AVAudioFifo **fifo, AVCodecContext *output_codec_context)
{
    /** Create the FIFO buffer based on the specified output sample format. */
    if (!(*fifo = av_audio_fifo_alloc(output_codec_context->sample_fmt,
                                      output_codec_context->channels, 1))) {
        fprintf(stderr, "Could not allocate FIFO\n");
        return AVERROR(ENOMEM);
    }
    return 0;
}

/**
 * Initialize a temporary storage for the specified number of audio samples.
 * The conversion requires temporary storage due to the different format.
 * The number of audio samples to be allocated is specified in frame_size.
 */
static int init_converted_samples(uint8_t ***converted_input_samples,
                                  AVCodecContext *output_codec_context,
                                  int frame_size)
{
    int error;
    /**
     * Allocate as many pointers as there are audio channels.
     * Each pointer will later point to the audio samples of the corresponding
     * channels (although it may be NULL for interleaved formats).
     */
    if (!(*converted_input_samples = (uint8_t**)calloc(output_codec_context->channels,
                                            sizeof(**converted_input_samples)))) {
        fprintf(stderr, "Could not allocate converted input sample pointers\n");
        return AVERROR(ENOMEM);
    }
    /**
     * Allocate memory for the samples of all channels in one consecutive
     * block for convenience.
     */
    if ((error = av_samples_alloc(*converted_input_samples, NULL,
                                  output_codec_context->channels,
                                  frame_size,
                                  output_codec_context->sample_fmt, 0)) < 0) {
        fprintf(stderr,
                "Could not allocate converted input samples (error '%s')\n",NULL);//get_error_text(error));
        av_freep(&(*converted_input_samples)[0]);
        free(*converted_input_samples);
        return error;
    }
    return 0;
}
/**
 * Convert the input audio samples into the output sample format.
 * The conversion happens on a per-frame basis, the size of which is specified
 * by frame_size.
 */
static int convert_samples(const uint8_t **input_data,
                           uint8_t **converted_data, const int frame_size,
                           SwrContext *resample_context, OutputStream *ost, AVFrame *frame)
{
    int error;
    int dst_nb_samples;

	
	/* convert samples from native format to destination codec format, using the resampler */
	/* compute destination number of samples */
   /*dst_nb_samples = av_rescale_rnd(swr_get_delay(resample_context, ost->enc->sample_rate) +  frame->nb_samples,
									ost->enc->sample_rate, ost->enc->sample_rate, AV_ROUND_UP);
	av_assert0(dst_nb_samples ==  frame->nb_samples);*/
	
    /** Convert the samples using the resampler. */
    if ((error = swr_convert(resample_context,
                             converted_data, frame_size,
                             input_data    ,  frame_size)) < 0) {
        fprintf(stderr, "Could not convert input samples (error '%s')\n",NULL);//get_error_text(error));
        return error;
    }
	
	/*frame->pts = dst_nb_samples;//av_rescale_q(ost->samples_count, (AVRational){1, ost->enc->sample_rate}, ost->enc->time_base);
    ost->samples_count += dst_nb_samples;
	ost->next_pts  += frame->nb_samples;*/	
    return 0;
}
/** Add converted input audio samples to the FIFO buffer for later processing. */
static int add_samples_to_fifo(AVAudioFifo *fifo,
                               uint8_t **converted_input_samples,
                               const int frame_size)
{
    int error;
    /**
     * Make the FIFO as large as it needs to be to hold both,
     * the old and the new samples.
     */
    if ((error = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) + frame_size)) < 0) {
        fprintf(stderr, "Could not reallocate FIFO\n");
        return error;
    }
    /** Store the new samples in the FIFO buffer. */
    if (av_audio_fifo_write(fifo, (void **)converted_input_samples,
                            frame_size) < frame_size) {
        fprintf(stderr, "Could not write data to FIFO\n");
        return AVERROR_EXIT;
    }
    return 0;
}


int avcodec_parameters_from_context(AVCodecContext *par,
                                      const AVCodecContext *codec)
  {
      //codec_parameters_reset(par);
  
      par->codec_type = codec->codec_type;
      par->codec_id   = codec->codec_id;
      par->codec_tag  = codec->codec_tag;
  
      par->bit_rate              = codec->bit_rate;
      par->bits_per_coded_sample = codec->bits_per_coded_sample;
      par->bits_per_raw_sample   = codec->bits_per_raw_sample;
      par->profile               = codec->profile;
      par->level                 = codec->level;
  
      switch (par->codec_type) {
      case AVMEDIA_TYPE_VIDEO:
          par->pix_fmt              = codec->pix_fmt;
          par->width               = codec->width;
          par->height              = codec->height;
          par->field_order         = codec->field_order;
          par->color_range         = codec->color_range;
          par->color_primaries     = codec->color_primaries;
          par->color_trc           = codec->color_trc;
          par->colorspace         = codec->colorspace;
          par->chroma_sample_location     = codec->chroma_sample_location;
          par->sample_aspect_ratio = codec->sample_aspect_ratio;
          par->delay         = codec->delay; //has_b_frames
          break;
      case AVMEDIA_TYPE_AUDIO:
          par->sample_fmt          = codec->sample_fmt;
          par->channel_layout  = codec->channel_layout;
          par->channels        = codec->channels;
          par->sample_rate     = codec->sample_rate;
          par->block_align     = codec->block_align;
          par->frame_size      = codec->frame_size;
          par->initial_padding = codec->initial_padding;
          par->seek_preroll    = codec->seek_preroll;
          break;
      case AVMEDIA_TYPE_SUBTITLE:
          par->width  = codec->width;
          par->height = codec->height;
          break;
      }
  
      if (codec->extradata) {
          par->extradata = (uint8_t *)av_mallocz(codec->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
          if (!par->extradata)
              return AVERROR(ENOMEM);
          memcpy(par->extradata, codec->extradata, codec->extradata_size);
          par->extradata_size = codec->extradata_size;
      }
  
      return 0;
  }

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

    /*printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
           av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
           av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
           av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
           pkt->stream_index);*/
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
    AVCodecContext *c;
    int i;

    /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }

    ost->st = avformat_new_stream(oc, NULL);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    c = avcodec_alloc_context3(*codec);
//avcodec_alloc_context3(*codec);
    if (codec == NULL) {
        fprintf(stderr, "Could not alloc an encoding context\n");
        exit(1);
    }
    ost->enc = c;

    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
        c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        c->bit_rate    = 64000;
        c->sample_rate = 44100;
        if ((*codec)->supported_samplerates) {
            c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
        break;

    case AVMEDIA_TYPE_VIDEO:
        c->codec_id = codec_id;

        c->bit_rate = 400000;
        /* Resolution must be a multiple of two. */
        c->width    = 352;
        c->height   = 288;
        /* timebase: This is the fundamental unit of time (in seconds) in terms
         * of which frame timestamps are represented. For fixed-fps content,
         * timebase should be 1/framerate and timestamp increments should be
         * identical to 1. */
        ost->st->time_base = (AVRational){ 1, STREAM_FRAME_RATE };
        c->time_base       = ost->st->time_base;

        c->gop_size      = 12; /* emit one intra frame every twelve frames at most */
        c->pix_fmt       = STREAM_PIX_FMT;
        if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
            /* just for testing, we also add B-frames */
            c->max_b_frames = 2;
        }
        if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
            /* Needed to avoid using macroblocks in which some coeffs overflow.
             * This does not happen with normal video, it just happens here as
             * the motion of the chroma plane does not match the luma plane. */
            c->mb_decision = 2;
        }
    break;

    default:
        break;
    }

    /* Some formats want stream headers to be separate. */
    if (oc->oformat->flags & AVFMT_GLOBALHEADER)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
                                  uint64_t channel_layout,
                                  int sample_rate, int nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    if (!frame) {
        fprintf(stderr, "Error allocating an audio frame\n");
        exit(1);
    }

    frame->format = sample_fmt;
    frame->channel_layout = channel_layout;
    frame->sample_rate = sample_rate;
    frame->nb_samples = nb_samples;

    if (nb_samples) {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) {
            fprintf(stderr, "Error allocating an audio buffer\n");
            exit(1);
        }
    }

    return frame;
}

int open_audio_input(InputStream * ist, OutputStream *ost){
	AVFrame* frame = avcodec_alloc_frame();
    if (!frame)
    {
        std::cout << "Error allocating the frame" << std::endl;
        return 1;
    }

    // you can change the file name "01 Push Me to the Floor.wav" to whatever the file is you're reading, like "myFile.ogg" or
    // "someFile.webm" and this should still work
    AVFormatContext* formatContext = NULL;
    if (avformat_open_input(&formatContext,ist->filename , NULL, NULL) != 0) //input_filename
    {
        av_free(frame);
        std::cout << "Error opening the file" << std::endl;
        return 1;
    }

    if (avformat_find_stream_info(formatContext, NULL) < 0)
    {
        av_free(frame);
        avformat_close_input(&formatContext);
        std::cout << "Error finding the stream info" << std::endl;
        return 1;
    }

    // Find the audio stream
    AVCodec* cdc = NULL;
    int streamIndex = av_find_best_stream(formatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &cdc, 0);
    if (streamIndex < 0)
    {
        av_free(frame);
        avformat_close_input(&formatContext);
        std::cout << "Could not find any audio stream in the file" << std::endl;
        return 1;
    }

    AVStream* audioStream = formatContext->streams[streamIndex];
    AVCodecContext* codecContext = audioStream->codec;
    codecContext->codec = cdc;

    if (avcodec_open2(codecContext, codecContext->codec, NULL) != 0)
    {
        av_free(frame);
        avformat_close_input(&formatContext);
        std::cout << "Couldn't open the context with the decoder" << std::endl;
        return 1;
    }
	
	
	//Saving data into  InputStream Struct
	ist->frame = frame;
	ist->formatContext = formatContext;
	ist->cdc = cdc;
	ist->codec_id = codecContext->codec_id;
	ist->codecContext = codecContext;
	ist->audioStream = audioStream;
	
	av_opt_set_int       (ost->swr_ctx, "in_channel_count",   ist->codecContext->channels,       0);
    av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     ist->codecContext->sample_rate,    0);
	av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", ist->codecContext->sample_fmt, 0);

	 /* initialize the resampling context */
        if ((swr_init(ost->swr_ctx)) < 0) {
            fprintf(stderr, "Failed to initialize the resampling context\n");
            exit(1);
        }
}

void close_audio_input(InputStream *ist){
	// Clean up!
    av_free(ist->frame);
    avcodec_close(ist->codecContext);
    avformat_close_input(&ist->formatContext);
}

static void open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    AVCodecContext *c;
    int nb_samples;
    int ret;
    AVDictionary *opt = NULL;

    c = ost->enc; //enc_ctx

    /* open it */
    av_dict_copy(&opt, opt_arg, 0);
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open audio codec: %s\n");//, av_err2str(ret));
        exit(1);
    }

    /* init signal generator */
    ost->t     = 0;
    ost->tincr = 2 * M_PI * 110.0 / c->sample_rate;
    /* increment frequency by 110 Hz per second */
    ost->tincr2 = 2 * M_PI * 110.0 / c->sample_rate / c->sample_rate;

    if (c->codec->capabilities & AV_CODEC_CAP_VARIABLE_FRAME_SIZE)
        nb_samples = 10000;
    else
        nb_samples = c->frame_size;

    ost->frame     = alloc_audio_frame(c->sample_fmt, c->channel_layout,
                                       c->sample_rate, nb_samples);
    ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_S16, c->channel_layout,
                                       c->sample_rate, nb_samples);

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codec, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }

    /* create resampler context */
        ost->swr_ctx = swr_alloc();
        if (!ost->swr_ctx) {
            fprintf(stderr, "Could not allocate resampler context\n");
            exit(1);
        }

        /* set options */
        /*av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
        av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
        av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);*/
        av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
        av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
        av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

}

/* Prepare a 16 bit dummy audio frame of 'frame_size' samples and
 * 'nb_channels' channels. */
void get_audio_frame(OutputStream *ost, InputStream *ist, int *finished)
{
    /*AVFrame *frame = ost->tmp_frame;
    int j, i, v;
    int16_t *q = (int16_t*)frame->data[0];

    // check if we want to generate more frames 
    if (av_compare_ts(ost->next_pts, ost->enc->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
        return NULL;

    for (j = 0; j <frame->nb_samples; j++) {
        v = (int)(sin(ost->t) * 10000);
        for (i = 0; i < ost->enc->channels; i++)
            *q++ = v;
        ost->t     += ost->tincr;
        ost->tincr += ost->tincr2;
    }

    frame->pts = ost->next_pts;
    ost->next_pts  += frame->nb_samples;

    return frame;*/
	
	
	/*AVFrame* frame;
	AVFormatContext * formatContext;
	AVCodec* cdc;
	AVCodecID codec_id;
	AVCodecContext* codecContext;
	AVStream* audioStream;
	char* filename;*/
	
	AVFrame *frame = ist->frame;
	
	/*if (av_compare_ts(ost->next_pts, ost->enc->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0){
						  printf("av compared failed in get_audio_frame");
        return;
					  }*/
	
	AVPacket readingPacket;
    av_init_packet(&readingPacket);
	int error;
	int gotFrame;
    // Read the packets in a loop
    if (error = av_read_frame(ist->formatContext, &readingPacket) == 0)
    {
        if (readingPacket.stream_index == ist->audioStream->index)
        {
            AVPacket decodingPacket = readingPacket;

            // Audio packets can have multiple audio frames in a single packet
            while (decodingPacket.size > 0)
            {
                // Try to decode the packet into a frame
                // Some frames rely on multiple packets, so we have to make sure the frame is finished before
                // we can use it
                gotFrame=0;
                int result = avcodec_decode_audio4(ist->codecContext, frame, &gotFrame, &decodingPacket);

                if (result >= 0 && gotFrame)
                {
                    decodingPacket.size -= result;
                    decodingPacket.data += result;

                    // We now have a fully decoded audio frame
		//printAudioFrameInfo(codecContext, frame);
				std::cout<<"GOT FRAME"<<std::endl;
                //ao_play(adevice, (char*)ist->frame->extended_data[0],ist->frame->linesize[0] );
				goto done;
                }
                else
                {
                    decodingPacket.size = 0;
                    decodingPacket.data = NULL;
                }
            }
        }
						std::cout<<"-----------------------"<<std::endl;

        // You *must* call av_free_packet() after each call to av_read_frame() or else you'll leak memory
        av_free_packet(&readingPacket);
    }
	else if(error<1){
		if (error == AVERROR_EOF)
            *finished = 1;
        else {
            printf("Could not read frame (error '%s')\n",NULL);//get_error_text(error));
			 *finished = 1;
            return;
        }
	}
    
	gotFrame = 0;
    // Some codecs will cause frames to be buffered up in the decoding process. If the CODEC_CAP_DELAY flag
    // is set, there can be buffered up frames that need to be flushed, so we'll do that
    if (ist->codecContext->codec->capabilities & CODEC_CAP_DELAY)
    {
        av_init_packet(&readingPacket);
        // Decode all the remaining frames in the buffer, until the end is reached
        while (avcodec_decode_audio4(ist->codecContext, frame, &gotFrame, &readingPacket) >= 0 && gotFrame)
        {
            // We now have a fully decoded audio frame
            //printAudioFrameInfo(codecContext, frame);

        }
    }
	
	done:
	if (*finished && gotFrame)
        *finished = 0;
    av_packet_unref(&readingPacket);
	
	std::cout<<"returning frame"<<std::endl;
		
	frame->pts = ost->next_pts;
	ost->next_pts  += frame->nb_samples;
	//return frame;
	
}

/** Initialize one data packet for reading or writing. */
static void init_packet(AVPacket *packet)
{
    av_init_packet(packet);
    /** Set the packet data and size so that it is recognized as being empty. */
    packet->data = NULL;
    packet->size = 0;
}

/** Decode one audio frame from the input file. */
static int decode_audio_frame(AVFrame *frame,
                              AVFormatContext *input_format_context,
                              AVCodecContext *input_codec_context,
                              int *data_present, int *finished,int streamIndex, OutputStream *ost)
{							  
    /** Packet used for temporary storage. */
    AVPacket input_packet;
    int error;
    init_packet(&input_packet);
	
	/*///check if we want to generate more frames 
    if (av_compare_ts(ost->next_pts,  ost->enc->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
        return NULL;*/
	
    /** Read one audio frame from the input file into a temporary packet. */
    if ((error = av_read_frame(input_format_context, &input_packet)) < 0) {
        /** If we are at the end of the file, flush the decoder below. */
        if (error == AVERROR_EOF)
            *finished = 1;
        else {
            fprintf(stderr, "Could not read frame (error '%s')\n",NULL);//get_error_text(error));
            return error;
        }
    }
	 if (input_packet.stream_index != streamIndex){
		 *data_present = 0;
			return error;
	 }
    /**
     * Decode the audio frame stored in the temporary packet.
     * The input audio stream decoder is used to do this.
     * If we are at the end of the file, pass an empty packet to the decoder
     * to flush it.
     */
	AVPacket decodingPacket = input_packet;
   /* if ((error = avcodec_decode_audio4(input_codec_context, frame,
                                       data_present, &input_packet)) < 0) {
        fprintf(stderr, "Could not decode frame (error '%s')\n",NULL);//get_error_text(error));
        av_packet_unref(&input_packet);
        return error;
    }
	else{
		if(data_present){
			decodingPacket.size -= result;
            decodingPacket.data += result;
		}
	}*/
	
	 // Audio packets can have multiple audio frames in a single packet
            while (decodingPacket.size > 0)
            {
                // Try to decode the packet into a frame
                // Some frames rely on multiple packets, so we have to make sure the frame is finished before
                // we can use it
                int gotFrame = 0;
                int result = avcodec_decode_audio4(input_codec_context, frame, data_present, &decodingPacket);

                if (result >= 0 && data_present)
                {
                    decodingPacket.size -= result;
                    decodingPacket.data += result;

                    // We now have a fully decoded audio frame
		//printAudioFrameInfo(codecContext, frame);
				std::cout<<"DECODED FRAME"<<std::endl;
				
				
                //ao_play(adevice, (char*)frame->extended_data[0],frame->linesize[0] );
                }
                else
                {
					fprintf(stderr, "Could not decode frame (error '%s')\n",NULL);//get_error_text(error));
					av_packet_unref(&input_packet);
                    decodingPacket.size = 0;
                    decodingPacket.data = NULL;
					return result;
                }
            }
			av_free_packet(&input_packet);

    /**
     * If the decoder has not been flushed completely, we are not finished,
     * so that this function has to be called again.
     */
    if (*finished && *data_present)
        *finished = 0;
    av_packet_unref(&input_packet);
    return 0;
}

/*
 * encode one audio frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int read_decode_convert_and_store(AVFormatContext *oc, OutputStream *ost, InputStream *ist, AVAudioFifo *fifo, int *finished){
    //AVPacket pkt = { 0 }; // data and size must be 0;
    AVFrame *frame=NULL;
    int dst_nb_samples;
	uint8_t **converted_input_samples = NULL;
    int data_present;
    int ret = AVERROR_EXIT;

   // av_init_packet(&pkt);
	//c = ost->enc;
    //c = ist->codecContext;

    //get_audio_frame(ost,ist, finished);
	
	 /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, ost->enc->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0){
		*finished =  1;
		goto cleanup;
	}
	
	/** Decode one frame worth of audio samples. */
    if (decode_audio_frame(ist->frame, ist->formatContext,
                           ist->codecContext, &data_present, finished, ist->audioStream->index, ost))
					goto cleanup;
	
	frame = ist->frame;
	ao_play(adevice, (char*)frame->extended_data[0],frame->linesize[0] );

	//frame->pts = ost->next_pts;
    //ost->next_pts  += frame->nb_samples;
	
	/**
     * If we are at the end of the file and there are no more samples
     * in the decoder which are delayed, we are actually finished.
     * This must not be treated as an error.
     */
    if (*finished && !data_present) {
        ret = 0;
        goto cleanup;
    }
	
    if (data_present) {
		
		//frame->pts = ost->next_pts;
		//ost->next_pts  += frame->nb_samples;
		
		                printf("DATA PRESENT   nb_samples: %i\n",frame->nb_samples);
		/** Initialize the temporary storage for the converted input samples. */
		 if (init_converted_samples(&converted_input_samples, ost->enc,
                                   frame->nb_samples)){
								   printf("init_converted_samples FAILED\n");
								    goto cleanup;
									}
		                printf("DP1\n");

		/**
         * Convert the input samples to the desired output sample format.
         * This requires a temporary storage provided by converted_input_samples.
         */
        if (convert_samples((const uint8_t**)frame->extended_data, converted_input_samples,
                            frame->nb_samples, ost->swr_ctx, ost, frame)){
								printf("convert_samples FAILED\n");
								goto cleanup;								
							}
printf("DP2\n");
		/** Add the converted input samples to the FIFO buffer for later processing. */
        if (add_samples_to_fifo(fifo, converted_input_samples,
                                frame->nb_samples))
								 goto cleanup;
		ret=0;


		//av_frame_free(&frame);
		
	}
	printf("DP3\n");
	cleanup:
		 if (converted_input_samples) {
			 	printf("DP3.1\n");
			av_freep(&converted_input_samples[0]);
				printf("DP3.2\n");
			free(converted_input_samples);
				printf("DP3.3\n");
		}
		printf("DP4\n");
		return (frame || data_present) ? 0 : 1;
}
/**************************************************************/
/* video output */

static AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    int ret;

    picture = av_frame_alloc();
    if (!picture)
        return NULL;

    picture->format = pix_fmt;
    picture->width  = width;
    picture->height = height;

    /* allocate the buffers for the frame data */
    ret = av_frame_get_buffer(picture, 32);
    if (ret < 0) {
        fprintf(stderr, "Could not allocate frame data.\n");
        exit(1);
    }

    return picture;
}

static void open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary *opt_arg)
{
    int ret;
    AVCodecContext *c = ost->enc;
    AVDictionary *opt = NULL;

    av_dict_copy(&opt, opt_arg, 0);

    /* open the codec */
    ret = avcodec_open2(c, codec, &opt);
    av_dict_free(&opt);
    if (ret < 0) {
        fprintf(stderr, "Could not open video codec: %s\n");//, av_err2str(ret));
        exit(1);
    }

    /* allocate and init a re-usable frame */
    ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
    if (!ost->frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    /* If the output format is not YUV420P, then a temporary YUV420P
     * picture is needed too. It is then converted to the required
     * output format. */
    ost->tmp_frame = NULL;
    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        ost->tmp_frame = alloc_picture(AV_PIX_FMT_YUV420P, c->width, c->height);
        if (!ost->tmp_frame) {
            fprintf(stderr, "Could not allocate temporary picture\n");
            exit(1);
        }
    }

    /* copy the stream parameters to the muxer */
    ret = avcodec_parameters_from_context(ost->st->codec, c);
    if (ret < 0) {
        fprintf(stderr, "Could not copy the stream parameters\n");
        exit(1);
    }
}

/* Prepare a dummy image. */
static void fill_yuv_image(AVFrame *pict, int frame_index,
                           int width, int height)
{
    int x, y, i, ret;

    /* when we pass a frame to the encoder, it may keep a reference to it
     * internally;
     * make sure we do not overwrite it here
     */
    ret = av_frame_make_writable(pict);
    if (ret < 0)
        exit(1);

    i = frame_index;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            pict->data[0][y * pict->linesize[0] + x] = x + y + i * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            pict->data[1][y * pict->linesize[1] + x] = 128 + y + i * 2;
            pict->data[2][y * pict->linesize[2] + x] = 64 + x + i * 5;
        }
    }
}

static AVFrame *get_video_frame(OutputStream *ost)
{
    AVCodecContext *c = ost->enc;

    /* check if we want to generate more frames */
    if (av_compare_ts(ost->next_pts, c->time_base,
                      STREAM_DURATION, (AVRational){ 1, 1 }) >= 0)
        return NULL;

    if (c->pix_fmt != AV_PIX_FMT_YUV420P) {
        /* as we only generate a YUV420P picture, we must convert it
         * to the codec pixel format if needed */
        if (!ost->sws_ctx) {
            ost->sws_ctx = sws_getContext(c->width, c->height,
                                          AV_PIX_FMT_YUV420P,
                                          c->width, c->height,
                                          c->pix_fmt,
                                          SCALE_FLAGS, NULL, NULL, NULL);
            if (!ost->sws_ctx) {
                fprintf(stderr,
                        "Could not initialize the conversion context\n");
                exit(1);
            }
        }
        fill_yuv_image(ost->tmp_frame, ost->next_pts, c->width, c->height);
        sws_scale(ost->sws_ctx,
                  (const uint8_t * const *)ost->tmp_frame->data, ost->tmp_frame->linesize,
                  0, c->height, ost->frame->data, ost->frame->linesize);
    } else {
        fill_yuv_image(ost->frame, ost->next_pts, c->width, c->height);
    }

    ost->frame->pts = ost->next_pts++;
    printf("video ost->next_pts :%i\n",ost->next_pts);
    return ost->frame;
}

/*
 * encode one video frame and send it to the muxer
 * return 1 when encoding is finished, 0 otherwise
 */
static int write_video_frame(AVFormatContext *oc, OutputStream *ost)
{
    int ret;
    AVCodecContext *c;
    AVFrame *frame;
    int got_packet = 0;
    AVPacket pkt = { 0 };

    c = ost->enc;

    frame = get_video_frame(ost);

    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
        fprintf(stderr, "Error encoding video frame: %s\n");//, av_err2str(ret));
			    printf("Error encoding video frame\n");

        exit(1);
    }
	    printf("avcodec_encode_video2\n");


    if (got_packet) {
        ret = write_frame(oc, &c->time_base, ost->st, &pkt);
		printf("video write_frame\n");
    } else {
        ret = 0;
		printf("video write_frame failed\n");
    }

    if (ret < 0) {
        fprintf(stderr,"Error while writing video frame: %x\n",ret);//, av_err2str(ret));
        exit(1);
    }

			printf("finished write_video_frame\n");

    return (frame || got_packet) ? 0 : 1;
}

/**
 * Initialize one input frame for writing to the output file.
 * The frame will be exactly frame_size samples large.
 */
static int init_output_frame(AVFrame **frame,
                             AVCodecContext *output_codec_context,
                             int frame_size)
{
    int error;
    /** Create a new frame to store the audio samples. */
    if (!(*frame = av_frame_alloc())) {
        fprintf(stderr, "Could not allocate output frame\n");
        return AVERROR_EXIT;
    }
    /**
     * Set the frame's parameters, especially its size and format.
     * av_frame_get_buffer needs this to allocate memory for the
     * audio samples of the frame.
     * Default channel layouts based on the number of channels
     * are assumed for simplicity.
     */
    (*frame)->nb_samples     = frame_size;
    (*frame)->channel_layout = output_codec_context->channel_layout;
    (*frame)->format         = output_codec_context->sample_fmt;
    (*frame)->sample_rate    = output_codec_context->sample_rate;
    /**
     * Allocate the samples of the created frame. This call will make
     * sure that the audio frame can hold as many samples as specified.
     */
    if ((error = av_frame_get_buffer(*frame, 0)) < 0) {
        fprintf(stderr, "Could allocate output frame samples (error '%s')\n",NULL);//get_error_text(error));
        av_frame_free(frame);
        return error;
    }
    return 0;
}
/** Global timestamp for the audio frames */
static int64_t pts = 0;
/** Encode one frame worth of audio to the output file. */
static int encode_audio_frame(AVFrame *frame,
                              AVFormatContext *output_format_context,
                              AVCodecContext *output_codec_context,
                              int *data_present,OutputStream *ost)
{
    /** Packet used for temporary storage. */
    AVPacket output_packet;
    int error;
    av_init_packet(&output_packet);
	output_packet.data = NULL;
    output_packet.size = 0;
	
	    int dst_nb_samples;

    /** Set a timestamp based on the sample rate for the container. */
    if (frame) {
        frame->pts = pts;
        pts += frame->nb_samples;
		
		ost->samples_count +=  frame->nb_samples;
		ost->next_pts  = pts;//= frame->nb_samples;
				printf("audio ost->next_pts :%i\n",ost->next_pts);
		/*dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, ost->enc->sample_rate) +  frame->nb_samples,
									ost->enc->sample_rate, ost->enc->sample_rate, AV_ROUND_UP);
	    av_assert0(dst_nb_samples ==  frame->nb_samples);
		frame->pts = dst_nb_samples;
		ost->samples_count +=  frame->pts;
	    ost->next_pts  += frame->nb_samples;*/
    }
    /**
     * Encode the audio frame and store it in the temporary packet.
     * The output audio stream encoder is used to do this.
     */
    if ((error = avcodec_encode_audio2(output_codec_context, &output_packet,
                                       frame, data_present)) < 0) {
        fprintf(stderr, "Could not encode frame (error '%s')\n",NULL);//get_error_text(error));
        av_packet_unref(&output_packet);
        return error;
    }
    /** Write one audio frame from the temporary packet to the output file. */
    if (*data_present) {
        /*if ((error = av_write_frame(output_format_context, &output_packet)) < 0) {
            fprintf(stderr, "Could not write frame (error '%s')\n",NULL);//get_error_text(error));
            av_packet_unref(&output_packet);
            return error;
        }*/
		if(error = write_frame(output_format_context,&ost->enc->time_base, ost->st, &output_packet)<0){
            fprintf(stderr, "Could not write frame (error '%i')\n",error);//get_error_text(error));
            av_packet_unref(&output_packet);
			return error;
		}
        av_packet_unref(&output_packet);
    }
    return 0;
}
/**
 * Load one audio frame from the FIFO buffer, encode and write it to the
 * output file.
 */
static int load_encode_and_write(AVAudioFifo *fifo,
                                 AVFormatContext *output_format_context,
                                 AVCodecContext *output_codec_context,OutputStream *ost)
{
    /** Temporary storage of the output samples of the frame written to the file. */
    AVFrame *output_frame;
	
    /**
     * Use the maximum number of possible samples per frame.
     * If there is less than the maximum possible frame size in the FIFO
     * buffer use this number. Otherwise, use the maximum possible frame size
     */
    const int frame_size = FFMIN(av_audio_fifo_size(fifo),
                                 output_codec_context->frame_size);
    int data_written;
    /** Initialize temporary storage for one output frame. */
    if (init_output_frame(&output_frame, output_codec_context, frame_size))
        return AVERROR_EXIT;
    /**
     * Read as many samples from the FIFO buffer as required to fill the frame.
     * The samples are stored in the frame temporarily.
     */
    if (av_audio_fifo_read(fifo, (void **)output_frame->data, frame_size) < frame_size) {
        fprintf(stderr, "Could not read data from FIFO\n");
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
    /** Encode one frame worth of audio samples. */
    if (encode_audio_frame(output_frame, output_format_context,
                           output_codec_context, &data_written,ost)) {
        av_frame_free(&output_frame);
        return AVERROR_EXIT;
    }
	
	printf("output_frame->nb_samples : %i\n",output_frame->nb_samples);
	printf("frame_size : %i\n",frame_size);

    av_frame_free(&output_frame);
    return 0;
}

/**
 * Initialize the audio resampler based on the input and output codec settings.
 * If the input and output sample formats differ, a conversion is required
 * libswresample takes care of this, but requires initialization.
 */
static int init_resampler(AVCodecContext *input_codec_context,
                          AVCodecContext *output_codec_context,
                          SwrContext **resample_context)
{
        int error;
        /**
         * Create a resampler context for the conversion.
         * Set the conversion parameters.
         * Default channel layouts based on the number of channels
         * are assumed for simplicity (they are sometimes not detected
         * properly by the demuxer and/or decoder).
         */
        *resample_context = swr_alloc_set_opts(NULL,
                                              av_get_default_channel_layout(output_codec_context->channels),
                                              output_codec_context->sample_fmt,
                                              output_codec_context->sample_rate,
                                              av_get_default_channel_layout(input_codec_context->channels),
                                              input_codec_context->sample_fmt,
                                              input_codec_context->sample_rate,
                                              0, NULL);
        if (!*resample_context) {
            fprintf(stderr, "Could not allocate resample context\n");
            return AVERROR(ENOMEM);
        }
        /**
        * Perform a sanity check so that the number of converted samples is
        * not greater than the number of samples to be converted.
        * If the sample rates differ, this case has to be handled differently
        */
        av_assert0(output_codec_context->sample_rate == input_codec_context->sample_rate);
        /** Open the resampler with the specified parameters. */
        if ((error = swr_init(*resample_context)) < 0) {
            fprintf(stderr, "Could not open resample context\n");
            swr_free(resample_context);
            return error;
        }
    return 0;
}

void aoLibInit(InputStream *ost){
  //initialize AO lib
    ao_initialize();

    int driver=ao_default_driver_id();
	AVSampleFormat sfmt=ost->codecContext->sample_fmt;
    if(sfmt==AV_SAMPLE_FMT_U8){
        printf("U8\n");

        sformat.bits=8;
    }else if(sfmt==AV_SAMPLE_FMT_S16 || sfmt ==AV_SAMPLE_FMT_S16P){
        printf("S16\n");
        sformat.bits=16;
    }else if(sfmt==AV_SAMPLE_FMT_S32){
        printf("S32\n");
        sformat.bits=32;
    }


    //sformat.bits = atoi(av_get_sample_fmt_name(codecContext->sample_fmt));
    sformat.channels=ost->codecContext->channels;
    sformat.rate=ost->codecContext->sample_rate;
    sformat.byte_format=AO_FMT_NATIVE;
    sformat.matrix=0;

	adevice=ao_open_live(driver,&sformat,NULL);
	std::cout << "FInished ao init" << std::endl;

    //end of init AO LIB
}

static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}

/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    OutputStream video_st = { 0 }, audio_st = { 0 };
	InputStream audio_in = {0};
    const char *filename;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
    AVCodec *audio_codec, *video_codec;
    int ret;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;
    AVDictionary *opt = NULL;
    int i;
	AVAudioFifo *fifo = NULL;

    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();

    if (argc < 3) {
        printf("usage: %s output_file\n"
               "API example program to output a media file with libavformat.\n"
               "This program generates a synthetic audio and video stream, encodes and\n"
               "muxes them into a file named output_file.\n"
               "The output format is automatically guessed according to the file extension.\n"
               "Raw images can also be output by using '%%d' in the filename.\n"
               "\n", argv[0]);
        return 1;
    }
	
	filename = argv[1];
	audio_in.filename = argv[2];
	
    for (i = 3; i+1 < argc; i+=2) {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
            av_dict_set(&opt, argv[i]+1, argv[i+1], 0);
    }

    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc)
        return 1;

    fmt = oc->oformat;

    /* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
        add_stream(&audio_st, oc, &audio_codec, fmt->audio_codec);
        have_audio = 1;
        encode_audio = 1;
    }

    /* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(oc, video_codec, &video_st, opt);

    if (have_audio)
        open_audio(oc, audio_codec, &audio_st, opt);

    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename);//, av_err2str(ret));
            return 1;
        }
    }

	/*opening audio to mux*/
	open_audio_input(&audio_in, &audio_st);
	aoLibInit(&audio_in);

	
	
	/*if (init_resampler(audio_in.codecContext, audio_st.enc,
                       &audio_st.swr_ctx)){
		fprintf(stderr, "Could initialize resampler\n");//, av_err2str(ret));
		return 1;
	}*/
	 if (init_fifo(&fifo, audio_st.enc)){
		 fprintf(stderr, "Could initialize fifo\n");//, av_err2str(ret));
		return 1;
	}
	
	/* Write the stream header, if any. */
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n");//,av_err2str(ret));
        return 1;
    }
	
    while (encode_video || encode_audio) {
		const int output_frame_size = audio_st.enc->frame_size;
        /* select the stream to encode */
        if (encode_video &&(!encode_audio || av_compare_ts(video_st.next_pts, video_st.enc->time_base,audio_st.next_pts, audio_st.enc->time_base) <= 0))
		{
            encode_video = !write_video_frame(oc, &video_st);
						 printf("write video frame\n");
						 //encode_audio=1;
        } else{// if(!encode_audio && av_compare_ts(audio_st.next_pts, audio_st.enc->time_base, video_st.next_pts, video_st.enc->time_base) <= 0){
			//break;
			int finished = 0;
			
			/**
			 * Make sure that there is one frame worth of samples in the FIFO
			 * buffer so that the encoder can do its work.
			 * Since the decoder's and the encoder's frame size may differ, we
			 * need to FIFO buffer to store as many frames worth of input samples
			 * that they make up at least one frame worth of output samples.
			 */
			 while (av_audio_fifo_size(fifo) < output_frame_size ) {
				  /**
				 * Decode one frame worth of audio samples, convert it to the
				 * output sample format and put it into the FIFO buffer.
				 */
				encode_audio = !read_decode_convert_and_store(oc, &audio_st, &audio_in, fifo, &finished);
				 /**
				 * If we are at the end of the input file, we continue
				 * encoding the remaining audio samples to the output file.
				 */
				 
				 printf("read_decode_convert_and_store cicle\n");

				if (finished)
					break;
			 }
			 printf("read_decode_convert_and_store\n");
			 /**
			 * If we have enough samples for the encoder, we encode them.
			 * At the end of the file, we pass the remaining samples to
			 * the encoder.
			 */
			while ((av_audio_fifo_size(fifo) >= output_frame_size ||(finished && av_audio_fifo_size(fifo) > 0))&& true)
				 /**
				 * Take one frame worth of audio samples from the FIFO buffer,
				 * encode it and write it to the output file.
				 */
				if (load_encode_and_write(fifo, oc, audio_st.enc,&audio_st))
					goto cleanup;
				
				printf("load_encode_and_write\n");
            
			/**
			 * If we are at the end of the input file and have encoded
			 * all remaining samples, we can exit this loop and finish.
			 */
			if (finished) {
							printf("finished\n");
				int data_written;
				/** Flush the encoder as it may have delayed frames. */
				do {
					if (encode_audio_frame(NULL, oc,
										   audio_st.enc, &data_written,&audio_st))
						goto cleanup;
				} while (data_written);
				break;
			printf("encode_audio_frame FINISHED\n");
			}
		}
    }

    /* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

				printf("av_write_trailer\n");

	
    /* Close each codec. */
    if (have_video)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

	cleanup:
    /* free the stream */
    avformat_free_context(oc);
	
	/*free ist*/
	close_audio_input(&audio_in);
	if (fifo)
        av_audio_fifo_free(fifo);

	 ao_shutdown();
	
    return 0;
}
