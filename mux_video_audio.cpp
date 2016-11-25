/**
*	Trying to mux audio and video into a single file

     g++ mux_video_audio.cpp -o muxvidaud $(pkg-config --cflags --libs libavformat libavcodec libswresample libswscale libavutil)
*   g++ mux_video_audio.cpp -o muxvidaud $(pkg-config --cflags --libs ao libavformat libavcodec libswresample libswscale libavutil)
*/
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
extern "C"
{
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
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
  
  /* Add an output stream. */
static void add_stream(OutputStream *ost, AVFormatContext *oc,
                       AVCodec **codec,
                       enum AVCodecID codec_id)
{
	
			printf("Accesed add stream\nCodecID %i\n",codec_id);
    AVCodecContext *c;
    int i;

  if((*codec) == NULL){
						printf("codec is null\n");

		 /* find the encoder */
    *codec = avcodec_find_encoder(codec_id);
    if (!(*codec)) {
        fprintf(stderr, "Could not find encoder for '%s'\n",
                avcodec_get_name(codec_id));
        exit(1);
    }
	
		 if (codec == NULL) {
			fprintf(stderr, "Could not alloc an encoding context\n");
			exit(1);
		}
	}

    ost->st = avformat_new_stream(oc, *codec);
    if (!ost->st) {
        fprintf(stderr, "Could not allocate stream\n");
        exit(1);
    }
    ost->st->id = oc->nb_streams-1;
    
	
	c = avcodec_alloc_context3(*codec);
   
    //ost->enc = c;

				printf("PEPE10\n");
								printf("%x\n",(**codec));


    switch ((*codec)->type) {
    case AVMEDIA_TYPE_AUDIO:
	c->channels = 2;
	        printf("c->channels %i\n",c->channels);//c->bit_rate    = 64000;

       c->sample_fmt  = (*codec)->sample_fmts ?
            (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        printf("c->bit_rate %i\n",c->bit_rate);//c->bit_rate    = 64000;
        printf("c->sample_rate %i\n",c->sample_rate);//c->sample_rate = 44100;
        /*if ((*codec)->supported_samplerates) {
            
        }*/
		if((*codec)->supported_samplerates!=NULL){
			printf("%x\n",(*codec)->supported_samplerates[0]);

		c->sample_rate = (*codec)->supported_samplerates[0];
            for (i = 0; (*codec)->supported_samplerates[i]; i++) {
				printf("(*codec)->supported_samplerates[i] %i\n",(*codec)->supported_samplerates[i]);
                if ((*codec)->supported_samplerates[i] == 44100)
                    c->sample_rate = 44100;
            }
		}
		else{
			c->sample_rate = 44100;
		}
						
		        //c->sample_rate = 44100;

        //c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        //c->channel_layout = AV_CH_LAYOUT_STEREO;
        if ((*codec)->channel_layouts) {
            c->channel_layout = (*codec)->channel_layouts[0];
            for (i = 0; (*codec)->channel_layouts[i]; i++) {
                if ((*codec)->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    c->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        //c->channels        = av_get_channel_layout_nb_channels(c->channel_layout);
        ost->st->time_base = (AVRational){ 1, c->sample_rate };
	
		
						printf("PEPE11\n");

        break;

    case AVMEDIA_TYPE_VIDEO:
					printf("PEPE10.1\n");

	 
		ost->enc = c;
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
		 /* Some formats want stream headers to be separate. */
    if(oc != NULL)
	if(oc->oformat->flags != NULL)
	if (oc->oformat->flags & AVFMT_GLOBALHEADER){
		if((*codec)->type ==AVMEDIA_TYPE_VIDEO){
				printf("PEPE13\n");

			c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}
		else
			ost->enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	printf("PEPE14\n");
	}
    break;

    default:
					printf("Default codec type\n");
        break;
    }
	
	
	printf("PEPE15\n");
}

static int open_audio(AVFormatContext **oc, AVCodec **codec, OutputStream *ost, AVDictionary *opt_arg, const char*inputAudio){
	/**************************************************************
	*	Input Audio Handling
	***************************************************************/
	std::cout << "accesed open audio" << std::endl;
	int ret;
	AVCodecContext *c;
	ost->frame = avcodec_alloc_frame();
	
	if (!ost->frame){
        std::cout << "Error allocating the audio frame" << std::endl;
        return 1;
    }
	AVFormatContext* audioformatContext; //= oc;
    if (avformat_open_input(&audioformatContext,inputAudio , NULL, NULL) != 0) {//input_filename
        av_free(ost->frame);
        std::cout << "Error opening the file" << std::endl;
        return 1;
    }

    if (avformat_find_stream_info(audioformatContext, NULL) < 0){
        av_free(ost->frame);
        avformat_close_input(&audioformatContext);
        std::cout << "Error finding the stream info" << std::endl;
        return 1;
    }

    // Find the audio stream
    AVCodec* cdc;// = codec;
    int streamIndex = av_find_best_stream(audioformatContext, AVMEDIA_TYPE_AUDIO, -1, -1, &cdc, 0);
    if (streamIndex < 0){
        av_free(ost->frame);
        avformat_close_input(&audioformatContext);
        std::cout << "Could not find any audio stream in the file" << std::endl;
        return 1;
    }

    ost->st = audioformatContext->streams[streamIndex];
    ost->enc = ost->st->codec;
    ost->enc->codec = cdc;

	*codec = cdc;
	c = ost->enc; 
    if (avcodec_open2(ost->enc, ost->enc->codec, NULL) != 0){
        av_free(ost->frame);
        avformat_close_input(&audioformatContext);
        std::cout << "Couldn't open the context with the decoder" << std::endl;
        return 1;
    }
	/* End audio handling*/
	/*************************************************************************************/

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
        av_opt_set_int       (ost->swr_ctx, "in_channel_count",   c->channels,       0);
        av_opt_set_int       (ost->swr_ctx, "in_sample_rate",     c->sample_rate,    0);
        av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt",      AV_SAMPLE_FMT_S16, 0);
        av_opt_set_int       (ost->swr_ctx, "out_channel_count",  c->channels,       0);
        av_opt_set_int       (ost->swr_ctx, "out_sample_rate",    c->sample_rate,    0);
        av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt",     c->sample_fmt,     0);

        /* initialize the resampling context */
        if ((ret = swr_init(ost->swr_ctx)) < 0) {
            fprintf(stderr, "Failed to initialize the resampling context\n");
            exit(1);
        }
	        std::cout << "finished acced open audio" << std::endl;
	
	*oc = audioformatContext;
}
 static bool get_audio_frame(OutputStream *ost, AVFormatContext *oc){
    AVFrame *frame = ost->frame;
    /*int j, i, v;
    int16_t *q = (int16_t*)frame->data[0];

    //check if we want to generate more frames 
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
    ost->next_pts  += frame->nb_samples;*/
	
	AVPacket readingPacket;
    av_init_packet(&readingPacket);
	
	if(av_read_frame(oc, &readingPacket) == 0){
		if (readingPacket.stream_index == ost->st->index){
				AVPacket decodingPacket = readingPacket;

				// Audio packets can have multiple audio frames in a single packet
				while (decodingPacket.size > 0)
				{
					// Try to decode the packet into a frame
					// Some frames rely on multiple packets, so we have to make sure the frame is finished before
					// we can use it
					int gotFrame = 0;
					int result = avcodec_decode_audio4(ost->enc, frame, &gotFrame, &decodingPacket);

					if (result >= 0 && gotFrame)
					{
						decodingPacket.size -= result;
						decodingPacket.data += result;

						// We now have a fully decoded audio frame
			//printAudioFrameInfo(codecContext, frame);

						        printf("decoded frame\n");

							ao_play(adevice, (char*)frame->extended_data[0],frame->linesize[0] );
					
								        printf("played frame\n");

					}
					else
					{
						decodingPacket.size = 0;
						decodingPacket.data = NULL;
					}
				}
		}
	}
	else{
			av_free_packet(&readingPacket);
			return false;
	}
	av_free_packet(&readingPacket);
	
	printf("finished get audio frame\n");

	return true;
}

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
		std::cout << "accesed open video" << std::endl;

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
	
	std::cout<<"finished open_video"<<std::endl;
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

    return ost->frame;
}

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
    /* rescale output packet timestamp values from codec to stream timebase */
    av_packet_rescale_ts(pkt, *time_base, st->time_base);
    pkt->stream_index = st->index;

    /* Write the compressed frame to the media file. */
    //log_packet(fmt_ctx, pkt);
    return av_interleaved_write_frame(fmt_ctx, pkt);
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
        exit(1);
    }

    if (got_packet) {
        ret = write_frame(oc, &c->time_base, ost->st, &pkt);
    } else {
        ret = 0;
    }

    if (ret < 0) {
        fprintf(stderr, "Error while writing video frame: %s\n");//, av_err2str(ret));
        exit(1);
    }

    return (frame || got_packet) ? 0 : 1;
}
  
  static void close_stream(AVFormatContext *oc, OutputStream *ost)
{
    avcodec_free_context(&ost->enc);
    av_frame_free(&ost->frame);
    av_frame_free(&ost->tmp_frame);
    sws_freeContext(ost->sws_ctx);
    swr_free(&ost->swr_ctx);
}
  
  
  void aoLibInit(OutputStream *ost){
  //initialize AO lib
    ao_initialize();

    int driver=ao_default_driver_id();
	AVSampleFormat sfmt=ost->enc->sample_fmt;
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
    sformat.channels=ost->enc->channels;
    sformat.rate=ost->enc->sample_rate;
    sformat.byte_format=AO_FMT_NATIVE;
    sformat.matrix=0;

	adevice=ao_open_live(driver,&sformat,NULL);
	        std::cout << "FInished ao init" << std::endl;

    //end of init AO LIB
  }
  
/**************************************************************/
/* media file output */

int main(int argc, char **argv)
{
    OutputStream video_st = { 0 }, audio_st = { 0 }, audio_in_st = { 0 };
    const char *filename, *inputVid, *inputAudio;
    AVOutputFormat *fmt;
    AVFormatContext *oc;
	AVFormatContext *audiofcontext=NULL;
    AVCodec *audio_codec, *audio_decodec, *video_codec;
    int ret;
    int have_video = 0, have_audio = 0;
    int encode_video = 0, encode_audio = 0;
    AVDictionary *opt = NULL;
    int i;

			        printf("PEPE1");

	
    /* Initialize libavcodec, and register all codecs and formats. */
    av_register_all();

    if (argc < 4) {
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
   inputVid = argv[2];
   inputAudio = argv[3];
    /*for (i = 2; i+1 < argc; i+=2) {
        if (!strcmp(argv[i], "-flags") || !strcmp(argv[i], "-fflags"))
            av_dict_set(&opt, argv[i]+1, argv[i+1], 0);
    }*/
	
	
	
	
	/**************************************************************
	*	Input Video  Handling
	***************************************************************/
	
	
	/* End video handling*/
	/*************************************************************************************/

	
		        printf("PEPE2\n");

	
	        printf("PEPE3\n");

	
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename);
    if (!oc) {
        printf("Could not deduce output format from file extension: using MPEG.\n");
        avformat_alloc_output_context2(&oc, NULL, "mpeg", filename);
    }
    if (!oc)
        return 1;

	
    fmt = oc->oformat;

	open_audio(&audiofcontext, &audio_decodec, &audio_in_st, opt, inputAudio);
		aoLibInit(&audio_in_st);
		printf("PEPE4\n");
		
		printf("%x\n",audiofcontext);
		printf("%i\n",audiofcontext);

		get_audio_frame(&audio_in_st, audiofcontext);
		get_audio_frame(&audio_in_st, audiofcontext);
		get_audio_frame(&audio_in_st, audiofcontext);

	//testing just audio
	/*while((get_audio_frame(&audio_st, audiofcontext))){
		;//ao_play(adevice, (char*)audio_st.tmp_frame->extended_data[0],audio_st.tmp_frame->linesize[0] );
		        //av_free_packet(&audio_st->tmp_frame);

	}*/
	printf("PEPE5\n");
	/* Add the audio and video streams using the default format codecs
     * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
			printf("PEPE5.1\n");
        add_stream(&video_st, oc, &video_codec, fmt->video_codec);
        have_video = 1;
        encode_video = 1;
    }
			printf("PEPE6\n");

    if ( fmt->audio_codec != AV_CODEC_ID_NONE) { //audiofcontext->oformat->audio_codec
		printf("PEPE7\n");
		//fprintf(stderr, "Could not find encoder for '%s'\n",
         //       avcodec_get_name( audio_st.enc->codec_id ));
		add_stream(&audio_st, oc, &audio_decodec, audio_in_st.enc->codec_id ); //audiofcontext->oformat->audio_codec  ---  audio_in_st.enc->codec_id
        have_audio = 1;
        encode_audio = 1;
		printf("PEPE8\n");
    }
			printf("PEPE9\n");
		//printf("%i\n",audiofcontext->oformat->audio_codec);

	/* Now that all the parameters are set, we can open the audio and
     * video codecs and allocate the necessary encode buffers. */
    if (have_video)
        open_video(oc, video_codec, &video_st, opt);

   /* if (have_audio)
        open_audio(oc, audio_codec, &audio_st, opt, inputAudio);*/
	    av_dump_format(oc, 0, filename, 1);

    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&oc->pb, filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            fprintf(stderr, "Could not open '%s': %s\n", filename);//, av_err2str(ret));
            return 1;
        }
    }
	
	

    /* Write the stream header, if any. */
    ret = avformat_write_header(oc, &opt);
    if (ret < 0) {
        fprintf(stderr, "Error occurred when opening output file: %s\n");//,av_err2str(ret));
        return 1;
    }
	
	/** 
	*   VIDEO AND AUDIO MIXING OCCURS HERE
	**/
	
	
	
	
	
	
	
	/* Write the trailer, if any. The trailer must be written before you
     * close the CodecContexts open when you wrote the header; otherwise
     * av_write_trailer() may try to use memory that was freed on
     * av_codec_close(). */
    av_write_trailer(oc);

    /* Close each codec. */
    if (have_video)
        close_stream(oc, &video_st);
    if (have_audio)
        close_stream(oc, &audio_st);

    if (!(fmt->flags & AVFMT_NOFILE))
        /* Close the output file. */
        avio_closep(&oc->pb);

    /* free the stream */
    avformat_free_context(oc);
	    ao_shutdown();

    return 0;
}
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	
	