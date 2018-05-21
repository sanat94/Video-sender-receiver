#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>

extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libavformat/avio.h>
    #include <libswscale/swscale.h>
}

void error(char *msg)
{
    perror(msg);
    exit(0);
}

void log_callback(void *ptr, int level, const char *fmt, va_list vargs)
{
   static char message[8192];
   const char *module = NULL;

    if (ptr)
    {
        AVClass *avc = *(AVClass**) ptr;
        module = avc->item_name(ptr);
    }
   vsnprintf(message, sizeof(message), fmt, vargs);

   std::cout << "LOG: " << message << std::endl;
}

int main(int argc, char** argv) {

    SwsContext *img_convert_ctx;
    AVCodec *codec = NULL;
    AVFormatContext* context = avformat_alloc_context();
    AVCodecContext* ccontext = avcodec_alloc_context3(codec);
    int video_stream_index;
    
    //av_log_set_callback(&log_callback);
    
    
    int sockfd, portno, n;

    struct sockaddr_in serv_addr;
    struct hostent *server;

    char buffer[256];

    av_register_all();
    avformat_network_init();

    if (argc < 3) {
       fprintf(stderr,"usage %s hostname port\n", argv[0]);
       exit(0);
    }
    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, 
         (char *)&serv_addr.sin_addr.s_addr,
         server->h_length);
    serv_addr.sin_port = htons(portno);
    if (connect(sockfd,(struct sockaddr *)&serv_addr,sizeof(serv_addr)) < 0) 
        error("ERROR connecting");
    printf("Please enter the message: ");
    bzero(buffer,256);
    fgets(buffer,255,stdin);
    n = write(sockfd,buffer,strlen(buffer));
    if (n < 0) 
         error("ERROR writing to socket");
    bzero(buffer,256);
    n = read(sockfd,buffer,255);
    if (n < 0) 
         error("ERROR reading from socket");
    printf("%s\n",buffer);
    	
    //open rtsp
    if(avformat_open_input(&context, "rtmp://192.168.56.1:1935/test/myStream",NULL,NULL) != 0){
        return EXIT_FAILURE;
    }

    if(avformat_find_stream_info(context,NULL) < 0){
        return EXIT_FAILURE;
    }

    //search video stream
    for(int i =0;i<context->nb_streams;i++){
        if(context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }

    AVPacket packet;
    av_init_packet(&packet);

    //open output file
    //AVOutputFormat* fmt = av_guess_format(NULL,"test2.mp4",NULL);
    AVFormatContext* oc = avformat_alloc_context();
    //oc->oformat = fmt;
    //avio_open2(&oc->pb, "test.mp4", AVIO_FLAG_WRITE,NULL,NULL);

    AVStream* stream=NULL;
    int cnt = 0;
    //start reading packets from stream and write them to file
    av_read_play(context);//play RTSP

    codec = avcodec_find_decoder(AV_CODEC_ID_FLV1);
    if (!codec) exit(1);

    avcodec_get_context_defaults3(ccontext, codec);
    avcodec_copy_context(ccontext,context->streams[video_stream_index]->codec);
    std::ofstream myfile;

    if (avcodec_open2(ccontext, codec, NULL) < 0) exit(1);

    img_convert_ctx = sws_getContext(ccontext->width, ccontext->height, ccontext->pix_fmt, ccontext->width, ccontext->height,
                            AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL); //sws_getContext (int srcW, int srcH, enum AVPixelFormat srcFormat, int dstW, int dstH, enum AVPixelFormat dstFormat, int flags, SwsFilter *srcFilter, SwsFilter *dstFilter, const double *param)

    int size = avpicture_get_size(AV_PIX_FMT_YUV420P, ccontext->width, ccontext->height);
    uint8_t* picture_buf = (uint8_t*)(av_malloc(size));
    AVFrame* pic = av_frame_alloc();
    AVFrame* picrgb = av_frame_alloc();
    int size2 = avpicture_get_size(AV_PIX_FMT_RGB24, ccontext->width, ccontext->height);
    uint8_t* picture_buf2 = (uint8_t*)(av_malloc(size2));
    avpicture_fill((AVPicture *) pic, picture_buf, AV_PIX_FMT_YUV420P, ccontext->width, ccontext->height);
    avpicture_fill((AVPicture *) picrgb, picture_buf2, AV_PIX_FMT_RGB24, ccontext->width, ccontext->height);

    while(av_read_frame(context,&packet)>=0 && cnt <1000)
    {//read 100 frames

        std::cout << "1 Frame: " << cnt << std::endl;
        if(packet.stream_index == video_stream_index){//packet is video
            std::cout << "2 Is Video" << std::endl;
            if(stream == NULL)
            {//create stream in file
                std::cout << "3 create stream" << std::endl;
                stream = avformat_new_stream(oc,context->streams[video_stream_index]->codec->codec);
                avcodec_copy_context(stream->codec,context->streams[video_stream_index]->codec);
                stream->sample_aspect_ratio = context->streams[video_stream_index]->codec->sample_aspect_ratio;
            }
            int check = 0;
            packet.stream_index = stream->id;
            std::cout << "4 decoding" << std::endl;
            int result = avcodec_decode_video2(ccontext, pic, &check, &packet);
            std::cout << "Bytes decoded " << result << " check " << check << std::endl;
            if(cnt > 100)//cnt < 0)
            {
                sws_scale(img_convert_ctx, pic->data, pic->linesize, 0, ccontext->height, picrgb->data, picrgb->linesize);
                std::stringstream name;
                name << "test" << cnt << ".ppm";
                myfile.open(name.str());
                myfile << "P3 " << ccontext->width << " " << ccontext->height << " 255\n";
                for(int y = 0; y < ccontext->height; y++)
                {
                    for(int x = 0; x < ccontext->width * 3; x++)
                        myfile << (int)(picrgb->data[0] + y * picrgb->linesize[0])[x] << " ";
                }
                myfile.close();
            }
            cnt++;
        }
        av_free_packet(&packet);
        av_init_packet(&packet);
    }
    

    av_free(pic);
    av_free(picrgb);
    av_free(picture_buf);
    av_free(picture_buf2);

    av_read_pause(context);
    avio_close(oc->pb);
    avformat_free_context(oc);

    return (EXIT_SUCCESS);
}
