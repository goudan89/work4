#include <iostream>
#include <stdio.h>
#include <vector>
#include "al.h"
#include "alc.h"
extern "C"
{

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libavutil/avutil.h"
#include "libavutil/opt.h"
};

#define BUFFER_NUM 12
#define	SERVICE_UPDATE_PERIOD 20
#define MAX_AUDIO_FRME_SIZE 192000

using namespace std;

//对其初始化
void createOpenAL(ALuint source) {
	ALfloat SourceP[] = { 0.0,0.0,0.0 };
	ALfloat SourceV[] = { 0.0,0.0,0.0 };
	ALfloat ListenerP[] = { 0.0,0.0 };
	ALfloat ListenerV[] = { 0.0,0.0,0.0 };
	ALfloat ListenerO[] = { 0.0,0.0,-1.0,0.0,1.0,0.0 };
	alSourcef(source, AL_PITCH, 1.0);
	alSourcef(source, AL_GAIN, 1.0);
	alSourcefv(source, AL_POSITION, SourceP);
	alSourcefv(source, AL_VELOCITY, SourceV);
	alSourcef(source, AL_REFERENCE_DISTANCE, 50.0f);
	alSourcei(source, AL_LOOPING, AL_FALSE);
}
//填充
void feedAudioData(ALuint source, ALuint alBufferId, int out_sample_rate, FILE* fpPCM_open)
{
	int ret = 0;
	int ndatasize = 4096;
	char ndata[4096 + 1] = { 0 };
	//fseek(fpPCM_open, seek_location, SEEK_SET);
	ret = fread(ndata, 1, ndatasize, fpPCM_open);
	alBufferData(alBufferId, AL_FORMAT_STEREO16, ndata, ndatasize, out_sample_rate);
	alSourceQueueBuffers(source, 1, &alBufferId);
}

int main(int argc, char* argv[])
{
	//ffmpeg对于封装文件进行解码
	AVCodec* opencodec;//对应一种编解码器
	AVStream* openStream;//代表音频流
	AVFormatContext* openformatContext;//封装格式上下文结构体
	AVCodecContext* pcodecContext;//编解码器上下文结构体
    

	avformat_network_init();
	openformatContext = avformat_alloc_context();//初始化
	if (avformat_open_input(&openformatContext, argv[1], NULL, NULL) != 0)//打开音频/视频流
	{
		cout << "Couldn't open the input file" << endl;
		return -1;
	}

	if (avformat_find_stream_info(openformatContext, NULL) < 0)//查找视频/音频流信息
	{
		cout << "Couldn't find the stream information" << endl;
		return -1;
	}
	av_dump_format(openformatContext, 0, argv[1], false);

	//查找音频流
	int index = -1;
	for (int i = 0; i < openformatContext->nb_streams; i++)
	{
		if (openformatContext->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
		{
			index = i;
			break;
		}
	}
	if (index == -1)
	{
		cout << "Couldn't find a audio stream" << endl;
		return -1;
	}

	//查找解码器
	openStream = openformatContext->streams[index];
	pcodecContext = avcodec_alloc_context3(NULL);
	avcodec_parameters_to_context(pcodecContext, openStream->codecpar);
	opencodec = avcodec_find_decoder(pcodecContext->codec_id);
	pcodecContext->pkt_timebase = openformatContext->streams[index]->time_base;
	if (opencodec == NULL)//查找解码器
	{
		cout << "Couldn't find decoder" << endl;
		return -1;
	}
	//打开解码器
	if (avcodec_open2(pcodecContext, opencodec, NULL) < 0)
	{
		cout << "Couldn't open decoder" << endl;
		return -1;
	}

	AVPacket* packet = (AVPacket*)av_malloc(sizeof(AVPacket));//压缩数	
	AVFrame* frame = av_frame_alloc();//解压缩数据
	SwrContext* swr = swr_alloc();//重采样
	//重采样参数设置
	enum AVSampleFormat in_sample_fmt = pcodecContext->sample_fmt;//输入采样格式
	enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;	//输出采样格式
	int in_sample_rate = pcodecContext->sample_rate;//输入采样率
	int out_sample_rate = in_sample_rate;	//输出采样率
	uint64_t in_ch_layout = pcodecContext->channel_layout;	//输入声道布局
	uint64_t out_ch_layout = AV_CH_LAYOUT_STEREO;	//输出声道布局为立体声
	swr_alloc_set_opts(swr, out_ch_layout, out_sample_fmt, out_sample_rate, in_ch_layout, in_sample_fmt, in_sample_rate, 0, NULL);
	swr_init(swr);
	int nb_out_channel = av_get_channel_layout_nb_channels(out_ch_layout);//输出声道个数

	//写PCM
	uint8_t* out_buffer = (uint8_t*)av_malloc(MAX_AUDIO_FRME_SIZE);
	int out_buffer_size;
	FILE* fpPCM = fopen("output.pcm", "wb");
	while (av_read_frame(openformatContext, packet) >= 0)//读取压缩数据
	{
		if (packet->stream_index == index)
		{

			int ret = avcodec_send_packet(pcodecContext, packet);//解码
			if (ret != 0)
			{
				cout << "Couldn't submit the packet to the decoder" << endl;
				exit(1);
			}
			int getopenvideo = avcodec_receive_frame(pcodecContext, frame);
			if (getopenvideo == 0)
			{
				swr_convert(swr, &out_buffer, MAX_AUDIO_FRME_SIZE, (const uint8_t * *)frame->data, frame->nb_samples);
				out_buffer_size = av_samples_get_buffer_size(NULL, nb_out_channel, frame->nb_samples, out_sample_fmt, 1);//抽样样本大小
				fwrite(out_buffer, 1, out_buffer_size, fpPCM);
			}
		}
		av_packet_unref(packet);
	}

	//openAL
	ALCdevice* pDevice = alcOpenDevice(NULL);
	ALCcontext* pContext = alcCreateContext(pDevice, NULL);
	ALuint source;
	alcMakeContextCurrent(pContext);
	if (alcGetError(pDevice) != ALC_NO_ERROR)
		return AL_FALSE;
	alGenSources(1, &source);
	if (alGetError() != AL_NO_ERROR)
	{
		cout << "Couldn't generate audio source" << endl;
		return -1;
	}

	createOpenAL(source);//初始化

	FILE* pcm = NULL;//打开PCM
	if ((pcm = fopen("output.pcm", "rb")) == NULL)
	{
		cout << "Failed open the PCM file" << endl;
		return -1;
	}

	ALuint alBufferArray[BUFFER_NUM];
	alGenBuffers(BUFFER_NUM, alBufferArray);

	int seek_location = 0;//填充数据
	for (int i = 0; i < BUFFER_NUM; i++)
	{
		feedAudioData(source, alBufferArray[i], out_sample_rate, pcm);
		//seek_location += 4096;

		
	}


	alSourcePlay(source);//播放

	ALint total_buf_count = 0;
	ALint buffer_count;
	ALint iState;
	ALuint bufferId;
	ALint iQueuedBuffers;

	while (true)
	{
		buffer_count = 0;
		alGetSourcei(source, AL_BUFFERS_PROCESSED, &buffer_count);
		total_buf_count += buffer_count;
		//total_buf_count += buffer_count;
		while (buffer_count > 0) {
			bufferId = 0;
			alSourceUnqueueBuffers(source, 1, &bufferId);
			feedAudioData(source, bufferId, out_sample_rate, pcm);
		//	seek_location += 4096;
			buffer_count -= 1;
		}
		alGetSourcei(source, AL_SOURCE_STATE, &iState);
		if (iState != AL_PLAYING) {
			alGetSourcei(source, AL_BUFFERS_QUEUED, &iQueuedBuffers);
			if (iQueuedBuffers) {
				alSourcePlay(source);
			}
			else {
				break;
			}
		}
	}

	alSourceStop(source);
	alSourcei(source, AL_BUFFER, 0);
	alDeleteSources(1, &source);
	alDeleteBuffers(BUFFER_NUM, alBufferArray);

	fclose(pcm);
	fclose(fpPCM);

	alcMakeContextCurrent(NULL);
	alcDestroyContext(pContext);
	pContext = NULL;
	alcCloseDevice(pDevice);
	pDevice = NULL;
	av_frame_free(&frame);
	av_free(out_buffer);
	swr_free(&swr);
	avcodec_close(pcodecContext);
	avformat_close_input(&openformatContext);

	return 0;
}