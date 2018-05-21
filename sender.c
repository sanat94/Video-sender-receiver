#include <stdio.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> 

#define __STDC_CONSTANT_MACROS
#define DEBUG true
#define MAX 1000

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavformat/avformat.h"
#include "libavdevice/avdevice.h"
#include "libavutil/mathematics.h"
#include "libavutil/time.h"
#include "libavformat/avio.h"
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libavdevice/avdevice.h>
#include <libavutil/time.h>
#ifdef __cplusplus
};
#endif
#endif

void error(char *msg)
{
    perror(msg);
    exit(0);
}


int main(int argc, char* argv[])
{
	AVOutputFormat *ofmt = NULL;
	//Input AVFormatContext and Output AVFormatContext
	AVFormatContext *ifmt_ctx = NULL, *ofmt_ctx = NULL;
	AVPacket pkt;
	const char *in_filename, *out_filename;
	int ret, i;
	int videoindex=-1;
	int frame_index=0;
	int64_t start_time=0;
	
	int sockfd, newsockfd, portno, clilen;
     char buffer[256];
     struct sockaddr_in serv_addr, cli_addr;

     int n;
     if (argc < 2) {
         fprintf(stderr,"ERROR, no port provided\n");
         exit(1);
     }

     sockfd = socket(AF_INET, SOCK_STREAM, 0);

     if (sockfd < 0) 
        error("ERROR opening socket");
     bzero((char *) &serv_addr, sizeof(serv_addr));
     portno = atoi(argv[1]);
     serv_addr.sin_family = AF_INET;

     serv_addr.sin_addr.s_addr = INADDR_ANY;
     serv_addr.sin_port = htons(portno);
     if (bind(sockfd, (struct sockaddr *) &serv_addr,
              sizeof(serv_addr)) < 0) 
              error("ERROR on binding");
     listen(sockfd,5);
     clilen = sizeof(cli_addr);
     newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
     if (newsockfd < 0) 
          error("ERROR on accept");
     bzero(buffer,256);
     n = read(newsockfd,buffer,255);
     if (n < 0) error("ERROR reading from socket");
     printf("Here is the message: %s\n",buffer);
     	
     	in_filename  = "rtmp://192.168.56.1:1935/live/myStream";//URL（Input file URL）
	
	/*in_filename = "/dev/video0";
	AVInputFormat *inputFormat =av_find_input_format("v4l2");
  	AVDictionary *options = NULL;
  	av_dict_set(&options, "framerate", "20", 0);
	*/
	
	out_filename = "rtmp://sana:8850@192.168.56.1:1935/test/myStream";// URL（Output URL）[RTMP]
	//out_filename = "rtp://233.233.233.233:6666";// URL（Output URL）[UDP]

	avdevice_register_all();
	av_register_all();
	//Network
	avformat_network_init();
   	
	//Input
	if ((ret = avformat_open_input(&ifmt_ctx, in_filename, 0, 0)) < 0) {    //if webcam source, add input format
		printf( "Could not open input file.");
		goto end;
	}
	if ((ret = avformat_find_stream_info(ifmt_ctx, 0)) < 0) { 		//if webacam source, add dictionary options
		printf( "Failed to retrieve input stream information");
		goto end;
	}

	// Search for input video codec info
	for(i=0; i<ifmt_ctx->nb_streams; i++) 
		if(ifmt_ctx->streams[i]->codec->codec_type==AVMEDIA_TYPE_VIDEO){
			videoindex=i;
			break;
		}
	
	//Print info about input format. Parameters- context to analyze, index of stream to dump info about, url to print(src or dest), 0 for input context and 1 for output context
	av_dump_format(ifmt_ctx, 1, in_filename, 0);

	 // Allocate an AVFormatContext for an output format
	avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_filename); //RTMP Parameters-output format context, output format, format name, file name
	//avformat_alloc_output_context2(&ofmt_ctx, NULL, "mpegts", out_filename);//UDP

	if (!ofmt_ctx) {
		printf( "Could not create output context\n");
		ret = AVERROR_UNKNOWN;
		goto end;
	}

	// The output container format
	ofmt = ofmt_ctx->oformat;

	// Allocating output streams
	for (i = 0; i < ifmt_ctx->nb_streams; i++) {
		//Create output AVStream according to input AVStream
		AVStream *in_stream = ifmt_ctx->streams[i];
		AVStream *out_stream = avformat_new_stream(ofmt_ctx, in_stream->codec->codec);
		if (!out_stream) {
			printf( "Failed allocating output stream\n");
			ret = AVERROR_UNKNOWN;
			goto end;
		}
		//Copy the settings of AVCodecContext
		ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
		if (ret < 0) {
			printf( "Failed to copy context from input to output stream codec context\n");
			goto end;
		}
		out_stream->codec->codec_tag = 0;
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			out_stream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
	}

	// Show output format info
	av_dump_format(ofmt_ctx, 1, out_filename, 1);
	
	//Open output file/URL
	if (!(ofmt->flags & AVFMT_NOFILE)) {
		ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
		if (ret < 0) {
			printf( "Could not open output URL '%s'", out_filename);
			goto end;
		}
	}
	
	//Write output file/URL header
	ret = avformat_write_header(ofmt_ctx, NULL);
	if (ret < 0) {
		printf( "Error occurred when opening output URL\n");
		goto end;
	}

	//Get the current time
	start_time=av_gettime();
	while (1) {
		AVStream *in_stream, *out_stream;
		//Get an AVPacket
		ret = av_read_frame(ifmt_ctx, &pkt);
		if(pkt.stream_index==videoindex){
		if (ret < 0)
			break;
		//FIX：No PTS (Example: Raw H.264)
		//Simple Write PTS
		/*if(pkt.pts==AV_NOPTS_VALUE){
			//Write PTS
			AVRational time_base1=ifmt_ctx->streams[videoindex]->time_base;
			//Duration between 2 frames (us)
			int64_t calc_duration=(double)AV_TIME_BASE/av_q2d(ifmt_ctx->streams[videoindex]->r_frame_rate);
			//Parameters
			pkt.pts=(double)(frame_index*calc_duration)/(double)(av_q2d(time_base1)*AV_TIME_BASE);
			pkt.dts=pkt.pts;
			pkt.duration=(double)calc_duration/(double)(av_q2d(time_base1)*AV_TIME_BASE);
		}
		//Important:Delay
		if(pkt.stream_index==videoindex){
			AVRational time_base=ifmt_ctx->streams[videoindex]->time_base;
			AVRational time_base_q={1,AV_TIME_BASE};
			int64_t pts_time = av_rescale_q(pkt.dts, time_base, time_base_q);
			int64_t now_time = av_gettime() - start_time;
			if (pts_time > now_time)
				av_usleep(pts_time - now_time);

		}
		*/

		in_stream  = ifmt_ctx->streams[pkt.stream_index];
		out_stream = ofmt_ctx->streams[pkt.stream_index];
		/* copy packet */
		//Convert PTS/DTS
		pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, (AV_ROUND_NEAR_INF|AV_ROUND_PASS_MINMAX));
		pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
		pkt.pos = -1;
		//Print to Screen
		if(pkt.stream_index==videoindex){
			printf("Send %8d video frames to output URL\n",frame_index);
			frame_index++;
		}
	n = write(newsockfd,"I got your message",18);
     if (n < 0) error("ERROR writing to socket");
		//ret = av_write_frame(ofmt_ctx, &pkt);
		ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

		if (ret < 0) {
			printf( "Error muxing packet\n");
			break;
		}
		
		av_packet_unref(&pkt);
		}
		
	}
	//Write file trailer
	av_write_trailer(ofmt_ctx);
	
end:
	avformat_close_input(&ifmt_ctx);
	
    	/* close output */
	if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
		avio_close(ofmt_ctx->pb);
	avformat_free_context(ofmt_ctx);
	if (ret < 0 && ret != AVERROR_EOF) {
		printf( "Error occurred.\n");
		return -1;
	}
	return 0;
}
