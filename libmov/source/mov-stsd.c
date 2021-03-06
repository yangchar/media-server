#include "file-reader.h"
#include "file-writer.h"
#include "mov-internal.h"
#include <assert.h>
#include <stdlib.h>
#include <errno.h>

// stsd: Sample Description Box

static int mp4_read_extra(struct mov_t* mov, const struct mov_box_t* box)
{
	int r;
	uint64_t p1, p2;
	p1 = file_reader_tell(mov->fp);
	r = mov_reader_box(mov, box);
	p2 = file_reader_tell(mov->fp);
	file_reader_skip(mov->fp, box->size - (p2 - p1));
	return r;
}

/*
aligned(8) abstract class SampleEntry (unsigned int(32) format) 
	extends Box(format){ 
	const unsigned int(8)[6] reserved = 0; 
	unsigned int(16) data_reference_index; 
}
*/
static int mov_read_sample_entry(struct mov_t* mov, struct mov_box_t* box, uint16_t* data_reference_index)
{
	box->size = file_reader_rb32(mov->fp);
	box->type = file_reader_rb32(mov->fp);
	file_reader_skip(mov->fp, 6); // const unsigned int(8)[6] reserved = 0;
	*data_reference_index = (uint16_t)file_reader_rb16(mov->fp); // ref [stsc] sample_description_index
	return 0;
}

/*
class AudioSampleEntry(codingname) extends SampleEntry (codingname){ 
	const unsigned int(32)[2] reserved = 0; 
	template unsigned int(16) channelcount = 2; 
	template unsigned int(16) samplesize = 16; 
	unsigned int(16) pre_defined = 0; 
	const unsigned int(16) reserved = 0 ; 
	template unsigned int(32) samplerate = { default samplerate of media}<<16; 
}
*/
static int mov_read_audio(struct mov_t* mov, struct mov_stsd_t* stsd)
{
	struct mov_box_t box;
	mov_read_sample_entry(mov, &box, &stsd->data_reference_index);
	stsd->object_type_indication = mov_tag_to_object(box.type);
	stsd->stream_type = MP4_STREAM_AUDIO;
	mov->track->tag = box.type;

#if 1
	// const unsigned int(32)[2] reserved = 0;
	file_reader_skip(mov->fp, 8);
#else
	file_reader_rb16(mov->fp); /* version */
	file_reader_rb16(mov->fp); /* revision level */
	file_reader_rb32(mov->fp); /* vendor */
#endif

	stsd->u.audio.channelcount = (uint16_t)file_reader_rb16(mov->fp);
	stsd->u.audio.samplesize = (uint16_t)file_reader_rb16(mov->fp);

#if 1
	// unsigned int(16) pre_defined = 0; 
	// const unsigned int(16) reserved = 0 ;
	file_reader_skip(mov->fp, 4);
#else
	file_reader_rb16(mov->fp); /* audio cid */
	file_reader_rb16(mov->fp); /* packet size = 0 */
#endif

	stsd->u.audio.samplerate = file_reader_rb32(mov->fp); // { default samplerate of media}<<16;

	// audio extra(avc1: ISO/IEC 14496-14:2003(E))
	box.size -= 36;
	return mp4_read_extra(mov, &box);
}

/*
class VisualSampleEntry(codingname) extends SampleEntry (codingname){ 
	unsigned int(16) pre_defined = 0; 
	const unsigned int(16) reserved = 0; 
	unsigned int(32)[3] pre_defined = 0; 
	unsigned int(16) width; 
	unsigned int(16) height; 
	template unsigned int(32) horizresolution = 0x00480000; // 72 dpi 
	template unsigned int(32) vertresolution = 0x00480000; // 72 dpi 
	const unsigned int(32) reserved = 0; 
	template unsigned int(16) frame_count = 1; 
	string[32] compressorname; 
	template unsigned int(16) depth = 0x0018; 
	int(16) pre_defined = -1; 
	// other boxes from derived specifications 
	CleanApertureBox clap; // optional 
	PixelAspectRatioBox pasp; // optional 
}
class AVCSampleEntry() extends VisualSampleEntry (��avc1��){
	AVCConfigurationBox config;
	MPEG4BitRateBox (); // optional
	MPEG4ExtensionDescriptorsBox (); // optional
}
class AVC2SampleEntry() extends VisualSampleEntry (��avc2��){
	AVCConfigurationBox avcconfig;
	MPEG4BitRateBox bitrate; // optional
	MPEG4ExtensionDescriptorsBox descr; // optional
	extra_boxes boxes; // optional
}
*/
static int mov_read_video(struct mov_t* mov, struct mov_stsd_t* stsd)
{
	struct mov_box_t box;
	mov_read_sample_entry(mov, &box, &stsd->data_reference_index);
	stsd->object_type_indication = mov_tag_to_object(box.type);
	stsd->stream_type = MP4_STREAM_VISUAL; 
	mov->track->tag = box.type;
#if 1
	 //unsigned int(16) pre_defined = 0; 
	 //const unsigned int(16) reserved = 0;
	 //unsigned int(32)[3] pre_defined = 0;
	file_reader_skip(mov->fp, 16);
#else
	file_reader_rb16(mov->fp); /* version */
	file_reader_rb16(mov->fp); /* revision level */
	file_reader_rb32(mov->fp); /* vendor */
	file_reader_rb32(mov->fp); /* temporal quality */
	file_reader_rb32(mov->fp); /* spatial quality */
#endif
	stsd->u.visual.width = (uint16_t)file_reader_rb16(mov->fp);
	stsd->u.visual.height = (uint16_t)file_reader_rb16(mov->fp);
	stsd->u.visual.horizresolution = file_reader_rb32(mov->fp); // 0x00480000 - 72 dpi
	stsd->u.visual.vertresolution = file_reader_rb32(mov->fp); // 0x00480000 - 72 dpi
	// const unsigned int(32) reserved = 0;
	file_reader_rb32(mov->fp); /* data size, always 0 */
	stsd->u.visual.frame_count = (uint16_t)file_reader_rb16(mov->fp);

	//string[32] compressorname;
	//uint32_t len = file_reader_r8(mov->fp);
	//file_reader_skip(mov->fp, len);
	file_reader_skip(mov->fp, 32);

	stsd->u.visual.depth = (uint16_t)file_reader_rb16(mov->fp);
	// int(16) pre_defined = -1;
	file_reader_skip(mov->fp, 2);

	// video extra(avc1: ISO/IEC 14496-15:2010(E))
	box.size -= 86;
	return mp4_read_extra(mov, &box);
}

static int mov_read_hint_sample_entry(struct mov_t* mov, struct mov_stsd_t* stsd)
{
	struct mov_box_t box;
	mov_read_sample_entry(mov, &box, &stsd->data_reference_index);
	return file_reader_skip(mov->fp, box.size - 16);
}

static int mov_read_meta_sample_entry(struct mov_t* mov, struct mov_stsd_t* stsd)
{
	struct mov_box_t box;
	mov_read_sample_entry(mov, &box, &stsd->data_reference_index);
	return file_reader_skip(mov->fp, box.size - 16);
}

int mov_read_stsd(struct mov_t* mov, const struct mov_box_t* box)
{
	uint32_t i, entry_count;
	struct mov_track_t* track = mov->track;

	file_reader_r8(mov->fp);
	file_reader_rb24(mov->fp);
	entry_count = file_reader_rb32(mov->fp);

	if (track->stsd_count < entry_count)
	{
		void* p = realloc(track->stsd, sizeof(struct mov_stsd_t) * entry_count);
		if (NULL == p) return ENOMEM;
		track->stsd = (struct mov_stsd_t*)p;
	}

	track->stsd_count = entry_count;
	for (i = 0; i < entry_count; i++)
	{
		if (MOV_AUDIO == track->handler_type)
		{
			mov_read_audio(mov, &track->stsd[i]);
		}
		else if (MOV_VIDEO == track->handler_type)
		{
			mov_read_video(mov, &track->stsd[i]);
		}
		else if (MOV_HINT == track->handler_type)
		{
			mov_read_hint_sample_entry(mov, &track->stsd[i]);
		}
		else if (MOV_META == track->handler_type)
		{
			mov_read_meta_sample_entry(mov, &track->stsd[i]);
		}
		else
		{
			assert(0);
		}
	}

	(void)box;
	return file_reader_error(mov->fp);
}

//static int mov_write_h264(const struct mov_t* mov)
//{
//	size_t size;
//	uint64_t offset;
//	const struct mov_track_t* track = mov->track;
//
//	size = 8 /* Box */;
//
//	offset = file_writer_tell(mov->fp);
//	file_writer_wb32(mov->fp, 0); /* size */
//	file_writer_wb32(mov->fp, MOV_TAG('a', 'v', 'c', 'C'));
//
//	mov_write_size(mov->fp, offset, size); /* update size */
//	return size;
//}

static int mov_write_video(const struct mov_t* mov, const struct mov_stsd_t* stsd)
{
	size_t size;
	uint64_t offset;
	assert(1 == stsd->data_reference_index);

	size = 8 /* Box */ + 8 /* SampleEntry */ + 70 /* VisualSampleEntry */;

	offset = file_writer_tell(mov->fp);
	file_writer_wb32(mov->fp, 0); /* size */
	file_writer_wb32(mov->fp, mov->track->tag); // "h264"

	file_writer_wb32(mov->fp, 0); /* Reserved */
	file_writer_wb16(mov->fp, 0); /* Reserved */
	file_writer_wb16(mov->fp, stsd->data_reference_index); /* Data-reference index */

	file_writer_wb16(mov->fp, 0); /* Reserved / Codec stream version */
	file_writer_wb16(mov->fp, 0); /* Reserved / Codec stream revision (=0) */
	file_writer_wb32(mov->fp, 0); /* Reserved */
	file_writer_wb32(mov->fp, 0); /* Reserved */
	file_writer_wb32(mov->fp, 0); /* Reserved */

	file_writer_wb16(mov->fp, stsd->u.visual.width); /* Video width */
	file_writer_wb16(mov->fp, stsd->u.visual.height); /* Video height */
	file_writer_wb32(mov->fp, 0x00480000); /* Horizontal resolution 72dpi */
	file_writer_wb32(mov->fp, 0x00480000); /* Vertical resolution 72dpi */
	file_writer_wb32(mov->fp, 0); /* reserved / Data size (= 0) */
	file_writer_wb16(mov->fp, 1); /* Frame count (= 1) */

	file_writer_w8(mov->fp, 0 /*strlen(compressor_name)*/); /* compressorname */
	file_writer_write(mov->fp, " ", 31); // fill empty

	file_writer_wb16(mov->fp, 0x18); /* Reserved */
	file_writer_wb16(mov->fp, 0xffff); /* Reserved */

	if(MOV_OBJECT_H264 == stsd->object_type_indication)
		size += mov_write_avcc(mov);
	else if(MOV_OBJECT_MP4V == stsd->object_type_indication)
		size += mov_write_esds(mov);
	else if (MOV_OBJECT_HEVC == stsd->object_type_indication)
		size += mov_write_hvcc(mov);

	mov_write_size(mov->fp, offset, size); /* update size */
	return size;
}

static int mov_write_audio(const struct mov_t* mov, const struct mov_stsd_t* stsd)
{
	size_t size;
	uint64_t offset;

	size = 8 /* Box */ + 8 /* SampleEntry */ + 20 /* AudioSampleEntry */;

	offset = file_writer_tell(mov->fp);
	file_writer_wb32(mov->fp, 0); /* size */
	file_writer_wb32(mov->fp, mov->track->tag); // "aac "

	file_writer_wb32(mov->fp, 0); /* Reserved */
	file_writer_wb16(mov->fp, 0); /* Reserved */
	file_writer_wb16(mov->fp, 1); /* Data-reference index */

	/* SoundDescription */
	file_writer_wb16(mov->fp, 0); /* Version */
	file_writer_wb16(mov->fp, 0); /* Revision level */
	file_writer_wb32(mov->fp, 0); /* Reserved */

	file_writer_wb16(mov->fp, stsd->u.audio.channelcount); /* channelcount */
	file_writer_wb16(mov->fp, stsd->u.audio.samplesize); /* samplesize */

	file_writer_wb16(mov->fp, 0); /* pre_defined */
	file_writer_wb16(mov->fp, 0); /* reserved / packet size (= 0) */

	file_writer_wb32(mov->fp, stsd->u.audio.samplerate); /* samplerate */

	if(MOV_OBJECT_AAC == stsd->object_type_indication)
		size += mov_write_esds(mov);

	mov_write_size(mov->fp, offset, size); /* update size */
	return size;
}

size_t mov_write_stsd(const struct mov_t* mov)
{
	size_t i, size;
	uint64_t offset;
	const struct mov_track_t* track = mov->track;

	size = 12 /* full box */ + 4 /* entry count */;

	offset = file_writer_tell(mov->fp);
	file_writer_wb32(mov->fp, 0); /* size */
	file_writer_write(mov->fp, "stsd", 4);
	file_writer_wb32(mov->fp, 0); /* version & flags */
	file_writer_wb32(mov->fp, track->stsd_count); /* entry count */

	for (i = 0; i < track->stsd_count; i++)
	{
		if (MOV_VIDEO == track->handler_type)
		{
			size += mov_write_video(mov, &track->stsd[i]);
		}
		else if (MOV_AUDIO == track->handler_type)
		{
			size += mov_write_audio(mov, &track->stsd[i]);
		}
		else
		{
			assert(0);
		}
	}

	mov_write_size(mov->fp, offset, size); /* update size */
	return size;
}
