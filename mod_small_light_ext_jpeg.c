/* 
**  mod_small_light_ext_jpeg.c -- extension for jpeg
*/

#include "mod_small_light.h"
#include "mod_small_light_ext_jpeg.h"
#include <string.h>
#include <stdlib.h>

/*
** defines.
*/
#define MAX_MARKERS 16

#define M_SOF       0xc0
#define M_SOF_MIN   (M_SOF + 0)
#define M_SOF_MAX   (M_SOF + 15)
#define M_SOI       0xd8
#define M_EOI       0xd9
#define M_SOS       0xdA
#define M_APP       0xe0
#define M_APP0      (M_APP + 0)
#define M_APP1      (M_APP + 1)
#define M_COM       0xfe // comment

static unsigned char jpeg_header[] = { 0xff, M_SOI };

/*
** functions.
*/
int load_exif_from_memory(
    unsigned char **exif_data,
    unsigned int *exif_size,
    request_rec *r,
    const unsigned char *data,
    unsigned int data_len)
{
    unsigned int data_len_bk=data_len;
    unsigned char *data_bk=data;

    // scan SOI marker.
    if (data_len <= 2) return 0;
    data_len -= 2;
    unsigned char c1 = *data++;
    unsigned char c2 = *data++;
    if (c1 != 0xff || c2 != M_SOI) {
	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "scan SOI return 0");
        return 0;
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checked SOI: Data Length = %#x",(data_len_bk - data_len ));

    int num_marker = 0;
    unsigned char *marker_data[MAX_MARKERS];
    unsigned int marker_size[MAX_MARKERS];    

    // scan marker.
    for (;;) {
        unsigned char c;
        for (;;) {
            c = *data++;
            if (data_len == 0) return 0;
            data_len--;
            if (c == 0xff) break;
        }
        for (;;) {
            c = *data++;
            if (data_len == 0) return 0;
            data_len--;
            if (c != 0xff) break;
        }

        // check marker.
        if (c == M_EOI || c == M_SOS || c == 0) {
	    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checked Marker: address = %#x",(data_len_bk - data_len ));

            break;
        } else if (c == M_APP1 || c == M_COM) {
            // get length of app1.
            unsigned int length;
            length =  (*data++ << 8);
            length += *(data++);

	    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checked App1/COM: address = %#x : c=%#x, length=%d, header=%s",(data - data_bk - 4),c,length,(char *)data);
            // validate length.
            if (length < 2) return 0;

            // get app1 pointer and length.
            if (num_marker < MAX_MARKERS) {
                marker_data[num_marker] = (unsigned char *)(data - 4);
                marker_size[num_marker] = length + 2;
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checked App1/COM: Num=%d, *marker_data=%#x, marker_size=%d",num_marker,*marker_data[num_marker],marker_size[num_marker]);
                num_marker++;
            }
            
            // skip pointer.
            if (data_len <= length) return 0;
            data_len -= length;
            data += length - 2;
        } else if (c == M_APP ) {
            // get length of App0(JFIF)
            unsigned int length;
	    length  =  (*data++ << 8) ;
	    length +=  (*data++);
	    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checked App0: address = %#x : c=%#x, length=%d, header=%s",(data - data_bk - 4),c,length,(char *)data);

            // validate length.
            if (length < 2) return 0;
            
            // skip pointer.
            if (data_len <= length) return 0;
            data_len -= length;
            data += length - 2;
        } else {
            // get length of others
            unsigned int length;
            length =  (*data++ << 8);
            length += *(data++);

	    char short_name[8];
	    if(c==0xd8){
		snprintf(short_name,8,"SOI");
	    } else if(c==0xc0){
		snprintf(short_name,8,"SOF0");
	    } else if(c==0xc2){
		snprintf(short_name,8,"SOF2");
	    } else if(c==0xc4){
		snprintf(short_name,8,"DHT");
	    } else if(c==0xdb){
		snprintf(short_name,8,"DQT");
	    } else if(c==0xdd){
		snprintf(short_name,8,"DRI");
	    } else if(c==0xda){
		snprintf(short_name,8,"SOS");
	    } else if(c>=0xd0 && c<=0xd7){
		snprintf(short_name,8,"RST%d",(int)(c-0xe0));
	    } else if(c>=0xe0 && c<=0xef){
		snprintf(short_name,8,"APP%d",(int)(c-0xe0));
	    } else if(c==0xfe){
		snprintf(short_name,8,"COM");
	    } else if(c==0xd9){
		snprintf(short_name,8,"EOI");
	    } else {
		snprintf(short_name,8,"Unknown");
	    }
	    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Checked %s: address = %#x : c=%#x, length=%d",short_name,(data - data_bk - 4),c,length);
            // validate length.
            if (length < 2) return 0;

            // skip pointer.
            if (data_len <= length) return 0;
            data_len -= length;
            data += length - 2;
        }
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Data Length = %d",data_len );

    // copy app1.
    int i;
    unsigned int exif_size_total = 0;
    for (i = 0; i < num_marker; i++) {
        exif_size_total += marker_size[i];
    }
    *exif_size = exif_size_total;
    *exif_data = apr_palloc(r->pool, exif_size_total);
    unsigned char *exif_data_ptr = *exif_data;
    for (i = 0; i < num_marker; i++) {
        memcpy(exif_data_ptr, marker_data[i], marker_size[i]);
        exif_data_ptr += marker_size[i];
    }

    ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "Exif size total = %d",exif_size_total );
    return 1;
}

void exif_brigade_insert_tail(
    unsigned char *exif_data, unsigned int exif_size,
    unsigned char *image_data, unsigned long image_size,
    request_rec *r, apr_bucket_brigade *bb)
{
    apr_bucket *b;
    b = apr_bucket_pool_create(jpeg_header, sizeof(jpeg_header), r->pool, bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);
    b = apr_bucket_pool_create(exif_data, exif_size, r->pool, bb->bucket_alloc);
    APR_BRIGADE_INSERT_TAIL(bb, b);
}

