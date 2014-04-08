#include <stdio.h>
#include <jpeglib.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

/* int width = 1600; */
/* int height = 1200; */
/* int bytes_per_pixel = 3;   /\* or 1 for GRACYSCALE images *\/ */
/* int color_space = JCS_RGB; /\* or JCS_GRAYSCALE for grayscale images *\/ */

#ifndef MAX
#  define MAX(a, b) ((a) > (b) ? (a) : (b))
#  define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif


typedef struct image_data_t {
    unsigned char *image;
    struct jpeg_decompress_struct cinfo;
    unsigned char min_rgb;
} image_data_t;

/**
 * dump image info
 *
 * @param pimage point to image_data_t struct
 */
void dump_jpeg_info(image_data_t *pimage) {
    printf("Geometry: %dx%d pixels\n", pimage->cinfo.image_width, pimage->cinfo.image_height);
    printf("BPP: %d\n", pimage->cinfo.num_components);
    printf("Color space: %d\n", pimage->cinfo.jpeg_color_space);
    printf("Min Rgb: %d\n", pimage->min_rgb);
}

/**
 * read jpeg file into a buffer
 *
 * @param fd file descriptor to read image from
 *
 * @returns NULL on failure, pointer to image_data_t structure on success
 */
image_data_t *read_jpeg_file(int fd) {
    image_data_t *result;

    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JSAMPROW row_pointer[1];
	
    unsigned long location = 0;
    int i = 0;
    int rgb_value;
	
    result = (image_data_t *)malloc(sizeof(image_data_t));
    if(!result) {
        perror("malloc");
        return NULL;
    }

    memset(result, 0, sizeof(image_data_t));

    FILE *infile = fdopen(fd, "rb");
    if (!infile) {
        perror("fdopen");
        free(result);
        return NULL;
    }

    result->cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&result->cinfo);
    jpeg_stdio_src(&result->cinfo, infile);
    jpeg_read_header(&result->cinfo, TRUE);

    jpeg_start_decompress(&result->cinfo);

    result->image = (unsigned char*)malloc(result->cinfo.output_width * 
                                           result->cinfo.output_height *
                                           result->cinfo.num_components);
    row_pointer[0] = (unsigned char *)malloc(result->cinfo.output_width *
                                             result->cinfo.num_components);

    result->min_rgb = 0xff;

    while(result->cinfo.output_scanline < result->cinfo.image_height) {
        jpeg_read_scanlines(&result->cinfo, row_pointer, 1);
        for(i = 0; i < result->cinfo.image_width * result->cinfo.num_components; i++) {
            if(!(i % 3)) {
                rgb_value = (row_pointer[0][i] * 0.30) +
                    (row_pointer[0][i+1] * 0.59) +
                    (row_pointer[0][i+2] * 0.11);
                rgb_value = MIN(rgb_value, 0xff);
                result->min_rgb = MIN(result->min_rgb, rgb_value);
            }

            result->image[location++] = row_pointer[0][i];
        }
    }

    jpeg_finish_decompress(&result->cinfo);
    jpeg_destroy_decompress(&result->cinfo);
    free(row_pointer[0]);
    fclose(infile);

    return result;
}

/**
 * dump a jpeg file out an fd
 *
 * @param fd file descriptor to dump to
 * @param pimage pointer to image_data_t struct to dump
 *
 * @returns 1 on success, 0 on failure
 */
int write_jpeg_file(int fd, image_data_t *pimage) {
    struct jpeg_error_mgr jerr;
    struct jpeg_compress_struct cinfo;
	
    /* this is a pointer to one row of image data */
    JSAMPROW row_pointer[1];
    FILE *outfile = fdopen(fd, "wb");
	
    if (!outfile) {
        perror("fdopen");
        return 0;
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);
    jpeg_stdio_dest(&cinfo, outfile);

    cinfo.image_width = pimage->cinfo.image_width;
    cinfo.image_height = pimage->cinfo.image_height;
    cinfo.input_components = pimage->cinfo.num_components;
    cinfo.in_color_space = JCS_RGB; //pimage->cinfo.jpeg_color_space;

    jpeg_set_defaults(&cinfo); /* ?? */

    jpeg_start_compress(&cinfo, TRUE);

    while(cinfo.next_scanline < cinfo.image_height) {
        row_pointer[0] = &pimage->image[cinfo.next_scanline * 
                                        pimage->cinfo.image_width *
                                        pimage->cinfo.num_components];
        jpeg_write_scanlines(&cinfo, row_pointer, 1);
    }

    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    fclose(outfile);

    return 1;
}

int blit_jpeg(image_data_t *pbackground, image_data_t *pforeground, int xoffset, int yoffset) {
    xoffset = MIN(xoffset, pbackground->cinfo.image_width - 1);
    yoffset = MIN(yoffset, pbackground->cinfo.image_height - 1);

    int xdim = MIN(pforeground->cinfo.image_width, pbackground->cinfo.image_width - xoffset);
    int ydim = MIN(pforeground->cinfo.image_height, pbackground->cinfo.image_height - yoffset);

    float base_alpha = 0.8;
    float alpha = 0.5;

    int x, y, round;
    float gs;

    for(y=0; y < ydim; y++) {
        unsigned char *linedst = &pbackground->image[(yoffset + y) *
                                                     pbackground->cinfo.image_width *
                                                     pbackground->cinfo.num_components];
        unsigned char *linesrc = &pforeground->image[y * pforeground->cinfo.image_width *
                                                     pforeground->cinfo.num_components];
        for(x=0; x < xdim; x++) {
            unsigned char *dst = linedst + (pbackground->cinfo.num_components * (x + xoffset));
            unsigned char *src = linesrc + (pforeground->cinfo.num_components * x);

            /* how bout some janky alpha masking? */
            gs = ((*src * 0.30) + (*(src + 1) * 0.59) + (*(src + 2) * 0.11));
            gs = ((gs - pforeground->min_rgb) * (255.0 / (255.0 - pforeground->min_rgb))) / 255.0;

            alpha = 1.0 - gs;

            for(round = 0; round < 3; round++) {
                *(dst + round) = (*(src + round) * alpha) + (*(dst + round) * (1.0 - alpha));
            }
        }
    }
}


/*
 * do the thing
 */
int main(int argc, char *argv[]) {
    int fd_main, fd_watermark, fd_out;
    int option;
    int x, y;

    x = 0;
    y = 0;

    while((option = getopt(argc, argv, "x:y:")) != -1) {
        switch(option) {
        case 'x':
            x = atoi(optarg);
            break;

        case 'y':
            y = atoi(optarg);
            break;

        default:
            fprintf(stderr, "Unknown option: %c", option);
            exit(EXIT_FAILURE);
        }
    }

    fd_main = open("in.jpg", O_RDONLY);
    fd_watermark = open("watermark.jpg", O_RDONLY);
    fd_out = open("out.jpg", O_WRONLY | O_CREAT, 0666);

    if((fd_main < 0) || (fd_watermark < 0) || (fd_out < 0)) {
        fprintf(stderr, "Error opening input/output files\n");
        exit(EXIT_FAILURE);
    }

    image_data_t *main = read_jpeg_file(fd_main);
    if(!main) exit(EXIT_FAILURE);

    printf("Input file data:\n");
    dump_jpeg_info(main);

    image_data_t *watermark = read_jpeg_file(fd_watermark);
    if(!watermark) exit(EXIT_FAILURE);

    printf("\nWatermark file data:\n");
    dump_jpeg_info(watermark);

    blit_jpeg(main, watermark, x, y);

    write_jpeg_file(fd_out, main);

    close(fd_main);
    close(fd_watermark);
    close(fd_out);

    return 0;
}


