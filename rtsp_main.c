#include<stdio.h>
#include<pthread.h>
#include<unistd.h>
#include<error.h>
#include<malloc.h>

#include"../inc/rtsp_session.h"

typedef struct AVRational_t{
	int num; ///< numerator
	int den; ///< denominator
} AVRational_t;

typedef struct h264_sps_t{

	int profile_idc;
	int level_idc;
	int transform_bypass;              ///< qpprime_y_zero_transform_bypass_flag
	int log2_max_frame_num;            ///< log2_max_frame_num_minus4 + 4
	int poc_type;                      ///< pic_order_cnt_type
	int log2_max_poc_lsb;              ///< log2_max_pic_order_cnt_lsb_minus4
	int delta_pic_order_always_zero_flag;
	int offset_for_non_ref_pic;
	int offset_for_top_to_bottom_field;
	int poc_cycle_length;              ///< num_ref_frames_in_pic_order_cnt_cycle
	int ref_frame_count;               ///< num_ref_frames
	int gaps_in_frame_num_allowed_flag;
	int mb_width;                      ///< frame_width_in_mbs_minus1 + 1
	int mb_height;                     ///< frame_height_in_mbs_minus1 + 1
	int frame_mbs_only_flag;
	int mb_aff;                        ///<mb_adaptive_frame_field_flag
	int direct_8x8_inference_flag;
	int crop;                   ///< frame_cropping_flag
	int crop_left;              ///< frame_cropping_rect_left_offset
	int crop_right;             ///< frame_cropping_rect_right_offset
	int crop_top;               ///< frame_cropping_rect_top_offset
	int crop_bottom;            ///< frame_cropping_rect_bottom_offset
	int vui_parameters_present_flag;
	AVRational_t sar;
	int timing_info_present_flag;
	uint32_t num_units_in_tick;
	uint32_t time_scale;
	int fixed_frame_rate_flag;
	short offset_for_ref_frame[256]; //FIXME dyn aloc?
	int bitstream_restriction_flag;
	int num_reorder_frames;
	int scaling_matrix_present;
	uint8_t scaling_matrix4[6][16];
	uint8_t scaling_matrix8[2][64];
}h264_sps_t;

typedef void* (thread_func)(void*);

int begin_thread(thread_func func,void* arg)
{
	int ret = 0;
	pthread_t pid;
	pthread_attr_t attr;

	pthread_attr_init(&attr);

	if (pthread_create(&pid,&attr,func,arg)) 
	{
//		ret = errno;
//		printf("pthread_create(func) failed with %d %d\n", ret, ENOMEM);
	}
	return ret;
}
unsigned int SpsWidth = 0;
unsigned int SpsHeight = 0;
int SpsProfileIdc;
char Sps_Pps_Param_Sets[1024];
static long longBytes = 0;

unsigned char memFile[1024 * 1024 * 5];

struct Nalu
{
	unsigned char data[1024 * 20];

	int len;
};

struct Nalu nalu[2000];
int NaluCount = 0;

static int FindStartCode(size_t pos)
{
	int ret = 0;

	while(pos < longBytes)
	{
		if(memFile[pos] == 0 && memFile[pos + 1] == 0 && memFile[pos + 2] == 0 && memFile[pos + 3] == 1)
		{
//			NaluCount++;

			break;
		}
		pos++;
	}

	ret = pos;

	return ret;
}


static int Memory_2_Nalu()
{
	int ret = 0;

	size_t StartCodePosition = 0;
	size_t NextStartCodePosition = 0;
	size_t frameLen = 0;
	unsigned char* pTmp = memFile;
	StartCodePosition = FindStartCode(0);
	while(1)
	{
		NextStartCodePosition = FindStartCode(StartCodePosition + 5);

		NaluCount++;

		frameLen = NextStartCodePosition - StartCodePosition;

/*
		if(NaluCount < 10)
		{
			printf("trj test frameLen = %d\n",frameLen);
		}
*/

		memcpy(nalu[NaluCount - 1].data,(char*)pTmp + StartCodePosition,frameLen);

		nalu[NaluCount - 1 ].len = frameLen;

//		ProcessNaluUnit(StartCodePosition,frameLen);

		if(NextStartCodePosition == longBytes)
		{
			printf("H264 file parse over\n");

			break;
		}

		StartCodePosition = NextStartCodePosition;
	}

	printf("the H264 file have %d Nal unit\n",NaluCount);

	return ret;
}

static void h264_decode_annexb( unsigned char *dst, int *dstlen, const unsigned char *src, const int srclen )
{
	unsigned char *dst_sav = dst;
	const unsigned char *end = &src[srclen];
	
	while (src < end)
	{
		if (src < end - 3 && src[0] == 0x00 && src[1] == 0x00 &&
			src[2] == 0x03)
		{
			*dst++ = 0x00;
			*dst++ = 0x00;
			
			src += 3;
			continue;
		}
		*dst++ = *src++;
	}
	
	*dstlen = dst - dst_sav;
}


typedef struct bs_t
{
    uint8_t *p_start;
    uint8_t *p;
    uint8_t *p_end;

    int     i_left;    /* i_count number of available bits */
} bs_t;

static inline void bs_skip( bs_t *s, int i_count )
{
    s->i_left -= i_count;

    while( s->i_left <= 0 )
    {
        s->p++;
        s->i_left += 8;
    }
}

static uint32_t bs_read( bs_t *s, int i_count )
{
     static const uint32_t i_mask[33] =
     {  0x00,
        0x01,      0x03,      0x07,      0x0f,
        0x1f,      0x3f,      0x7f,      0xff,
        0x1ff,     0x3ff,     0x7ff,     0xfff,
        0x1fff,    0x3fff,    0x7fff,    0xffff,
        0x1ffff,   0x3ffff,   0x7ffff,   0xfffff,
        0x1fffff,  0x3fffff,  0x7fffff,  0xffffff,
        0x1ffffff, 0x3ffffff, 0x7ffffff, 0xfffffff,
        0x1fffffff,0x3fffffff,0x7fffffff,0xffffffff};
    int      i_shr;
    uint32_t i_result = 0;

    while( i_count > 0 )
    {
        if( s->p >= s->p_end )
        {
            break;
        }

        if( ( i_shr = s->i_left - i_count ) >= 0 )
        {
            /* more in the buffer than requested */
            i_result |= ( *s->p >> i_shr )&i_mask[i_count];
            s->i_left -= i_count;
            if( s->i_left == 0 )
            {
                s->p++;
                s->i_left = 8;
            }
            return( i_result );
        }
        else
        {
            /* less in the buffer than requested */
           i_result |= (*s->p&i_mask[s->i_left]) << -i_shr;
           i_count  -= s->i_left;
           s->p++;
           s->i_left = 8;
        }
    }

    return( i_result );
}

static inline uint32_t bs_read1( bs_t *s )
{
    if( s->p < s->p_end )
    {
        unsigned int i_result;

        s->i_left--;
        i_result = ( *s->p >> s->i_left )&0x01;
        if( s->i_left == 0 )
        {
            s->p++;
            s->i_left = 8;
        }
        return i_result;
    }

    return 0;
}

static inline void bs_init( bs_t *s, void *p_data, int i_data )
{
    s->p_start = (uint8_t*)p_data;
    s->p       = (uint8_t*)p_data;
    s->p_end   = s->p + i_data;
    s->i_left  = 8;
}

static inline int bs_read_ue( bs_t *s )
{
	int i = 0;

	while( bs_read1( s ) == 0 && s->p < s->p_end && i < 32 )
	{
		i++;
	}
	return( ( 1 << i) - 1 + bs_read( s, i ) );
}

static inline int bs_read_se( bs_t *s )
{
	int val = bs_read_ue( s );

	return val&0x01 ? (val+1)/2 : -(val/2);
}


/////trj changed for pointer///
static void h264_decode_hrd_parameters(bs_t* s, h264_sps_t* p_sps)
{
	int cpb_count, i;
	cpb_count = bs_read_ue(s) + 1;
	bs_read(s, 4); /* bit_rate_scale */
	bs_read(s, 4); /* cpb_size_scale */
	for(i=0; i<cpb_count; i++){
		bs_read_ue(s); /* bit_rate_value_minus1 */
		bs_read_ue(s); /* cpb_size_value_minus1 */
		bs_read(s, 1);     /* cbr_flag */
	}
	bs_read(s, 5); /* initial_cpb_removal_delay_length_minus1 */
	bs_read(s, 5); /* cpb_removal_delay_length_minus1 */
	bs_read(s, 5); /* dpb_output_delay_length_minus1 */
	bs_read(s, 5); /* time_offset_length */
}

static bool h264_decode_seq_parameter_set(uint8_t* p_nal,  int n_nal_size, h264_sps_t* p_sps)
{
	uint8_t *pb_dec = NULL;
	int     i_dec = 0;
	bs_t s;
	int i_sps_id;

	int nal_hrd_parameters_present_flag, vcl_hrd_parameters_present_flag;

	pb_dec = p_nal;
	i_dec = n_nal_size; 

	bs_init( &s, pb_dec, i_dec );

	// profile(8)
	p_sps->profile_idc = bs_read( &s, 8);

	/* constraint_set012, reserver(5), level(8) */
	bs_skip( &s, 1+1+1 + 5 + 8 );
	/* sps id */
	i_sps_id = bs_read_ue( &s );
	if( i_sps_id >= 32/*SPS_MAX*/ )
	{
		printf("invalid SPS (sps_id=%d)", i_sps_id );
		return false;
	}

	p_sps->scaling_matrix_present = 0;
	if(p_sps->profile_idc >= 100)		//high profile
	{ 
		if(bs_read_ue(&s) == 3)			//chroma_format_idc
			bs_read(&s, 1);				//residual_color_transform_flag
		bs_read_ue(&s);					//bit_depth_luma_minus8
		bs_read_ue(&s);					//bit_depth_chroma_minus8
		p_sps->transform_bypass = bs_read(&s, 1);
		bs_skip(&s, 1); //decode_scaling_matrices(h, sps, NULL, 1, sps->scaling_matrix4, sps->scaling_matrix8);
	}

	/* Skip i_log2_max_frame_num */
	p_sps->log2_max_frame_num = bs_read_ue( &s );
	if( p_sps->log2_max_frame_num > 12)
		p_sps->log2_max_frame_num = 12;
	/* Read poc_type */
	p_sps->poc_type/*->i_pic_order_cnt_type*/ = bs_read_ue( &s );
	if( p_sps->poc_type == 0 )
	{
		/* skip i_log2_max_poc_lsb */
		p_sps->log2_max_poc_lsb/*->i_log2_max_pic_order_cnt_lsb*/ = bs_read_ue( &s );
		if( p_sps->log2_max_poc_lsb > 12 )
			p_sps->log2_max_poc_lsb = 12;
	}
	else if( p_sps->poc_type/*p_sys->i_pic_order_cnt_type*/ == 1 )
	{
		int i_cycle;
		/* skip b_delta_pic_order_always_zero */
		p_sps->delta_pic_order_always_zero_flag/*->i_delta_pic_order_always_zero_flag*/ = bs_read( &s, 1 );
		/* skip i_offset_for_non_ref_pic */
		bs_read_se( &s );
		/* skip i_offset_for_top_to_bottom_field */
		bs_read_se( &s );
		/* read i_num_ref_frames_in_poc_cycle */
		i_cycle = bs_read_ue( &s );
		if( i_cycle > 256 ) i_cycle = 256;
		while( i_cycle > 0 )
		{
			/* skip i_offset_for_ref_frame */
			bs_read_se(&s );
			i_cycle--;
		}
	}
	/* i_num_ref_frames */
	bs_read_ue( &s );
	/* b_gaps_in_frame_num_value_allowed */
	bs_skip( &s, 1 );

	/* Read size */
	p_sps->mb_width/*->fmt_out.video.i_width*/  = 16 * ( bs_read_ue( &s ) + 1 );
	p_sps->mb_height/*fmt_out.video.i_height*/ = 16 * ( bs_read_ue( &s ) + 1 );

	/* b_frame_mbs_only */
	p_sps->frame_mbs_only_flag/*->b_frame_mbs_only*/ = bs_read( &s, 1 );
	if( p_sps->frame_mbs_only_flag == 0 )
	{
		bs_skip( &s, 1 );
	}
	/* b_direct8x8_inference */
	bs_skip( &s, 1 );

	/* crop */
	p_sps->crop = bs_read( &s, 1 );
	if( p_sps->crop )
	{
		/* left */
		bs_read_ue( &s );
		/* right */
		bs_read_ue( &s );
		/* top */
		bs_read_ue( &s );
		/* bottom */
		bs_read_ue( &s );
	}

	/* vui */
	p_sps->vui_parameters_present_flag = bs_read( &s, 1 );
	if( p_sps->vui_parameters_present_flag )
	{
		int aspect_ratio_info_present_flag = bs_read( &s, 1 );
		if( aspect_ratio_info_present_flag )
		{
			static const struct { int num, den; } sar[17] =
			{
				{ 0,   0 }, { 1,   1 }, { 12, 11 }, { 10, 11 },
				{ 16, 11 }, { 40, 33 }, { 24, 11 }, { 20, 11 },
				{ 32, 11 }, { 80, 33 }, { 18, 11 }, { 15, 11 },
				{ 64, 33 }, { 160,99 }, {  4,  3 }, {  3,  2 },
				{  2,  1 },
			};

			int i_sar = bs_read( &s, 8 );

			if( i_sar < 17 )
			{
				p_sps->sar.num = sar[i_sar].num;
				p_sps->sar.den = sar[i_sar].den;
			}
			else if( i_sar == 255 )
			{
				p_sps->sar.num = bs_read( &s, 16 );
				p_sps->sar.den = bs_read( &s, 16 );
			}
			else
			{
				p_sps->sar.num = 0;
				p_sps->sar.den = 0;
			}

			//if( den != 0 )
			//	p_dec->fmt_out.video.i_aspect = (int64_t)VOUT_ASPECT_FACTOR *
			//	( num * p_dec->fmt_out.video.i_width ) /
			//	( den * p_dec->fmt_out.video.i_height);
			//else
			//	p_dec->fmt_out.video.i_aspect = VOUT_ASPECT_FACTOR;
		}
		else
		{
			p_sps->sar.num = 0;
			p_sps->sar.den = 0;
		}

		if(bs_read(&s, 1))		/* overscan_info_present_flag */
		{
			bs_read(&s, 1);     /* overscan_appropriate_flag */
		}

		if(bs_read(&s, 1))		/* video_signal_type_present_flag */
		{      
			bs_read(&s, 3);		/* video_format */
			bs_read(&s, 1);     /* video_full_range_flag */

			if(bs_read(&s, 1))  /* colour_description_present_flag */
			{
				bs_read(&s, 8);	/* colour_primaries */
				bs_read(&s, 8); /* transfer_characteristics */
				bs_read(&s, 8); /* matrix_coefficients */
			}
		}

		if(bs_read(&s, 1))		/* chroma_location_info_present_flag */
		{
			bs_read_ue(&s);		/* chroma_sample_location_type_top_field */
			bs_read_ue(&s);		/* chroma_sample_location_type_bottom_field */
		}

		p_sps->timing_info_present_flag = bs_read(&s, 1);
		if(p_sps->timing_info_present_flag)
		{
			p_sps->num_units_in_tick = bs_read(&s, 32);
			p_sps->time_scale = bs_read(&s, 32);
			p_sps->fixed_frame_rate_flag = bs_read(&s, 1);
		}

		nal_hrd_parameters_present_flag = bs_read(&s, 1);
		if(nal_hrd_parameters_present_flag)
			h264_decode_hrd_parameters(&s, p_sps);
		vcl_hrd_parameters_present_flag = bs_read(&s, 1);
		if(vcl_hrd_parameters_present_flag)
			h264_decode_hrd_parameters(&s, p_sps);
		if(nal_hrd_parameters_present_flag || vcl_hrd_parameters_present_flag)
			bs_read(&s, 1);     /* low_delay_hrd_flag */
		bs_read(&s, 1);         /* pic_struct_present_flag */

		p_sps->bitstream_restriction_flag = bs_read(&s, 1);
		if(p_sps->bitstream_restriction_flag)
		{
			unsigned int num_reorder_frames;
			bs_read(&s, 1);     /* motion_vectors_over_pic_boundaries_flag */
			bs_read_ue(&s); /* max_bytes_per_pic_denom */
			bs_read_ue(&s); /* max_bits_per_mb_denom */
			bs_read_ue(&s); /* log2_max_mv_length_horizontal */
			bs_read_ue(&s); /* log2_max_mv_length_vertical */
			num_reorder_frames= bs_read_ue(&s);
			bs_read_ue(&s); /*max_dec_frame_buffering*/

			if(num_reorder_frames > 16 /*max_dec_frame_buffering || max_dec_frame_buffering > 16*/){
				printf("illegal num_reorder_frames %d\n", num_reorder_frames);
				return false;
			}

			p_sps->num_reorder_frames= num_reorder_frames;
		}
	}

	return true;
}


inline static unsigned int Base64Encode(char* pData, unsigned int dataSize, char** base64)
	{
		const char base_64[128] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

		int	padding;
		unsigned int	i = 0, j = 0;
		
		char* in;
		unsigned int	outSize;
		char* out;

		unsigned long iOut = ((dataSize + 2) / 3) * 4;
		outSize = iOut += 2 * ((iOut / 60) + 1);
//		out = new BYTE [outSize];
		out = (char*) malloc(outSize);

		in = pData;

		if (outSize < (dataSize * 4 / 3)) return 0;

		while (i < dataSize) 	{
			padding = 3 - (dataSize - i);
			if (padding == 2) {
				out[j] = base_64[in[i]>>2];
				out[j+1] = base_64[(in[i] & 0x03) << 4];
				out[j+2] = '=';
				out[j+3] = '=';
			} else if (padding == 1) {
				out[j] = base_64[in[i]>>2];
				out[j+1] = base_64[((in[i] & 0x03) << 4) | ((in[i+1] & 0xf0) >> 4)];
				out[j+2] = base_64[(in[i+1] & 0x0f) << 2];
				out[j+3] = '=';
			} else{
				out[j] = base_64[in[i]>>2];
				out[j+1] = base_64[((in[i] & 0x03) << 4) | ((in[i+1] & 0xf0) >> 4)];
				out[j+2] = base_64[((in[i+1] & 0x0f) << 2) | ((in[i+2] & 0xc0) >> 6)];
				out[j+3] = base_64[in[i+2] & 0x3f];
			}
			i += 3;
			j += 4;
		}
		out[j] = '\0';
		*base64 = (char*)out;
//		delete [] out;

		free(out);
		return j;
	}

void* H264File_Read2SendThread(void* pUser)
{
	FILE* pf = fopen("test.264","rb");

	if(pf == NULL)
	{
		printf("open file failed \n");
	}

	fseek(pf, 0 , SEEK_SET);

	fseek(pf ,0 ,SEEK_END);

	longBytes = ftell(pf);

	fseek(pf, 0 , SEEK_SET);

	size_t readSize = fread(memFile,1,longBytes,pf);

	Memory_2_Nalu();

	fclose(pf);

	pf = NULL;

	int 	SendNaluCount = 0;
	bool b_find_sps = false;
	bool b_find_pps = false;
	int i_nal_type = 0;

	struct h264_sps_t	sps;
	char	sps_base64[1024];
	char	pps_base64[1024];
	char	sprop_parameter_sets[1024];

	while(SendNaluCount < NaluCount)
	{
		i_nal_type = nalu[SendNaluCount].data[4] & 0x1F;

/*
		{
			static int print = 0;
			if(print < 10 )
			{
				print++;
				printf("trj test i_nal_type = %d\n",i_nal_type);
			}
		}
*/

		if ( !b_find_sps )
		{
			if ( i_nal_type == 7)
			{
				uint8_t tmpBuffer[1024];

				int tmpLen = 0;

				h264_decode_annexb(tmpBuffer,(int *)&tmpLen,nalu[SendNaluCount].data + 5, nalu[SendNaluCount].len - 5);

				h264_decode_seq_parameter_set(tmpBuffer,tmpLen,&sps);

				if (sps.time_scale)
				{
					Base64Encode(nalu[SendNaluCount].data + 1, nalu[SendNaluCount].len - 1, &sps_base64);

					b_find_sps = true;
				}
				else
				{
					printf("The SPS's time_scale field info is error!\n");
				}
			}
		}

		if ( !b_find_pps )
		{
			if ( i_nal_type == 8 )
			{
					b_find_pps = true;
				
					Base64Encode(nalu[SendNaluCount].data + 1, nalu[SendNaluCount].len - 1, &pps_base64);
			}
			if (b_find_sps && b_find_pps)
			{
				strcpy(sprop_parameter_sets , sps_base64);

				strncat(sprop_parameter_sets ,"," , 1);

				strncat(sprop_parameter_sets ,pps_base64 , strlen(pps_base64));

				strcpy(Sps_Pps_Param_Sets , sprop_parameter_sets);
				
//				((MediaStreamH264*)(pRtspService->m_pMediaStream_Video))->Init(0,NETWORK_MTU, sps.mb_width, sps.mb_height, sps.profile_idc, sprop_parameter_sets);
			}
		}

		SendNaluCount++;

		if(b_find_pps && b_find_sps)
		{
//			printf("trj test sps & pps found now !!!!!!!!!!!!!!!!!!!!!\n");

			break;
		}
	}
	
	int64_t	frame_count = 0;

	int64_t i_pts = 0;

	SendNaluCount = 0;

	while(SendNaluCount < NaluCount)
	{
		usleep(60 * 1000);

		if(0 == g_RtpRun)
			continue;

		i_pts = frame_count * 90000 * 2 /30;

		{
			static int print = 0;
//			if(print < 10)
			{
				rtsp_TransportH264Nal(nalu[SendNaluCount].data + 1 ,  nalu[SendNaluCount].len - 1 , i_pts , 1);

				print++;

				printf("trj test send data now !!!!!!!!!!!!!!!!!!\n");
			}
		}

		frame_count++;

		SendNaluCount++;

		SendNaluCount = SendNaluCount % NaluCount;
/*
		if( pRtspService->m_pMediaStream_Video->m_bRun == FALSE)
			continue;

		h264_get_nal_type(&i_nal_type, nalu[SendNaluCount].data + 1);
		

		if ( h264_find_frame_end(&found_frame_start, nalu[SendNaluCount].data + 1, nalu[SendNaluCount].len - 1, i_nal_type) )
		{
			frame_count++;

			int64_t i_pts = h264_get_pts_rtp(&iframe_offset, frame_count, &sps, nalu[SendNaluCount].data + 1, nalu[SendNaluCount].len - 1, i_nal_type);

			pRtspService->m_pMediaStream_Video->TransportData(nalu[SendNaluCount].data + 1, nalu[SendNaluCount].len - 1, i_pts);
		}

		SendNaluCount++;

		SendNaluCount = SendNaluCount % NaluCount;
*/
	}

	return 0;
}

int main()
{
	int ret = 0;

//	printf("trj test c lang rtsp version now!!!!!!!!!!!!!!!!!!\n");

	m_pRtspTransport = (struct RtspTransport*)malloc(sizeof(struct RtspTransport));

	begin_thread(H264File_Read2SendThread,(void*)m_pRtspTransport);

	rtsp_session_open();

	while(1)
	{
		usleep(1000 * 10);
	}

	return ret;
}
