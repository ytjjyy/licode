#include <math.h>   // sqrt, round etc.
#include "media/MediaProcessor.h"

extern "C" {
#include <libavutil/mathematics.h>
}


#include <string>
#include <cstring>

#include "rtp/RtpVP8Fragmenter.h"
#include "rtp/RtpHeaders.h"
#include "media/codecs/VideoCodec.h"

using std::memcpy;

namespace erizo {

    DEFINE_LOGGER(InputProcessor, "media.InputProcessor");
    DEFINE_LOGGER(OutputProcessor, "media.OutputProcessor");

    MediaSink* audioSink = NULL;

    InputProcessor::InputProcessor() {
        audioDecoder = 0;
        videoDecoder = 0;
        lastVideoTs_ = 0;

        audioUnpackager = 1;
        videoUnpackager = 1;
        gotUnpackagedFrame_ = false;
        upackagedSize_ = 0;
        decodedBuffer_ = NULL;
        unpackagedBuffer_ = NULL;
        unpackagedBufferPtr_ = NULL;
        decodedAudioBuffer_ = NULL;
        unpackagedAudioBuffer_ = NULL;

        av_register_all();
    }

    InputProcessor::~InputProcessor() {
        this->close();
    }

    int InputProcessor::init(const MediaInfo& info, RawDataReceiver* receiver) {
        this->mediaInfo = info;
        this->rawReceiver_ = receiver;
        if (mediaInfo.hasVideo) {
            mediaInfo.videoCodec.codec = VIDEO_CODEC_VP8;
            decodedBuffer_ = (unsigned char*) malloc(
                    info.videoCodec.width * info.videoCodec.height * 3 / 2);
            unpackagedBufferPtr_ = unpackagedBuffer_ = (unsigned char*) malloc(UNPACKAGED_BUFFER_SIZE);
            if (!vDecoder.initDecoder(mediaInfo.videoCodec)) {
                // TODO(javier) check this condition
            }
            videoDecoder = 1; 
            if (!this->initVideoUnpackager()) {
                // TODO(javier) check this condition
            }
        }
        if (mediaInfo.hasAudio) {
            ELOG_DEBUG("Init AUDIO processor");
            decodedAudioBuffer_ = (unsigned char*) malloc(UNPACKAGED_BUFFER_SIZE);
            unpackagedAudioBuffer_ = (unsigned char*) malloc(
                    UNPACKAGED_BUFFER_SIZE);
            this->initAudioDecoder();
            this->initAudioUnpackager();
        }
        return 0;
    }

    int InputProcessor::deliverAudioData_(char* buf, int len) {
        if (audioDecoder && audioUnpackager) {
            ELOG_DEBUG("Decoding audio");
            int unp = unpackageAudio((unsigned char*) buf, len,
                    unpackagedAudioBuffer_);
            int a = decodeAudio(unpackagedAudioBuffer_, unp, decodedAudioBuffer_);
            ELOG_DEBUG("DECODED AUDIO a %d", a);
            RawDataPacket p;
            p.data = decodedAudioBuffer_;
            p.type = AUDIO;
            p.length = a;
            rawReceiver_->receiveRawData(p);
            return a;
        }
        return 0;
    }
    int InputProcessor::deliverVideoData_(char* buf, int len) {
        if (videoUnpackager && videoDecoder) {
            int ret = unpackageVideo(reinterpret_cast<unsigned char*>(buf), len, unpackagedBufferPtr_, &gotUnpackagedFrame_);
            if (ret < 0)
                return 0;
            upackagedSize_ += ret;
            unpackagedBufferPtr_ += ret;
            if (gotUnpackagedFrame_) {
                unpackagedBufferPtr_ -= upackagedSize_;
                ELOG_DEBUG("Tengo un frame desempaquetado!! Size = %d", upackagedSize_);
                int c;
                int gotDecodedFrame = 0;

                c = vDecoder.decodeVideo(unpackagedBufferPtr_, upackagedSize_,
                        decodedBuffer_,
                        mediaInfo.videoCodec.width * mediaInfo.videoCodec.height * 3 / 2,
                        &gotDecodedFrame);

                upackagedSize_ = 0;
                gotUnpackagedFrame_ = 0;
                ELOG_DEBUG("Bytes dec = %d", c);
                if (gotDecodedFrame && c > 0) {
                    ELOG_DEBUG("Tengo un frame decodificado!!");
                    gotDecodedFrame = 0;
                    RawDataPacket p;
                    p.data = decodedBuffer_;
                    p.length = c;
                    p.type = VIDEO;
                    rawReceiver_->receiveRawData(p);
                }

                return c;
            }
        }
        return 0;
    }

    bool InputProcessor::initAudioDecoder() {
        return true;
    }

    bool InputProcessor::initAudioUnpackager() {
        audioUnpackager = 1;
        return true;
    }

    bool InputProcessor::initVideoUnpackager() {
        videoUnpackager = 1;
        return true;
    }

    int InputProcessor::decodeAudio(unsigned char* inBuff, int inBuffLen, unsigned char* outBuff) {
        if (audioDecoder == 0) {
            //ELOG_DEBUG("No se han inicializado los parámetros del audioDecoder");
            return -1;
        }

        return 0;
    }

    int InputProcessor::unpackageAudio(unsigned char* inBuff, int inBuffLen, unsigned char* outBuff) {
        int l = inBuffLen - RtpHeader::MIN_SIZE;
        if (l < 0){
            ELOG_ERROR ("Error unpackaging audio");
            return 0;
        }
        memcpy(outBuff, &inBuff[RtpHeader::MIN_SIZE], l);

        return l;
    }

    int InputProcessor::unpackageVideo(unsigned char* inBuff, int inBuffLen, unsigned char* outBuff, int* gotFrame) {
        if (videoUnpackager == 0) {
            ELOG_DEBUG("Unpackager not correctly initialized");
            return -1;
        }

        int inBuffOffset = 0;
        *gotFrame = 0;
        RtpHeader* head = reinterpret_cast<RtpHeader*>(inBuff);
        if (head->getPayloadType() != 100) {
            return -1;
        }

        int l = inBuffLen - head->getHeaderLength();
        inBuffOffset+=head->getHeaderLength();

        erizo::RTPPayloadVP8* parsed = pars.parseVP8((unsigned char*) &inBuff[inBuffOffset], l);
        memcpy(outBuff, parsed->data, parsed->dataLength);
        if (head->getMarker()) {
            *gotFrame = 1;
        }
        int ret = parsed->dataLength;
        delete parsed;
        return ret;
    }

    void InputProcessor::closeSink(){
        this->close();
    }

    void InputProcessor::close(){
        if (audioDecoder == 1) {
            avcodec_close(aDecoderContext);
            av_free(aDecoderContext);
            audioDecoder = 0;
        }

        if (videoDecoder == 1) {
            vDecoder.closeDecoder();      
            videoDecoder = 0;
        }
        free(decodedBuffer_); decodedBuffer_ = NULL;
        free(unpackagedBuffer_); unpackagedBuffer_ = NULL;
        free(unpackagedAudioBuffer_); unpackagedAudioBuffer_ = NULL;
        free(decodedAudioBuffer_); decodedAudioBuffer_ = NULL;
    }

    OutputProcessor::OutputProcessor() {
        audioCoder = 0;
        videoCoder = 0;

        audioPackager = 0;
        videoPackager = 0;
        timestamp_ = 0;

        encodedBuffer_ = NULL;
        packagedBuffer_ = NULL;
        rtpBuffer_ = NULL;
        encodedAudioBuffer_ = NULL;
        packagedAudioBuffer_ = NULL;

        avcodec_register_all();
        av_register_all();
    }

    OutputProcessor::~OutputProcessor() {
        this->close();
    }

    int OutputProcessor::init(const MediaInfo& info, RTPDataReceiver* rtpReceiver) {
        this->mediaInfo = info;
        this->rtpReceiver_ = rtpReceiver;

        encodedBuffer_ = (unsigned char*) malloc(UNPACKAGED_BUFFER_SIZE);
        packagedBuffer_ = (unsigned char*) malloc(PACKAGED_BUFFER_SIZE);
        rtpBuffer_ = (unsigned char*) malloc(PACKAGED_BUFFER_SIZE);
        if(info.processorType == PACKAGE_ONLY){
            this->initVideoPackager();
            this->initAudioPackager();
            return 0;
        }
        if (mediaInfo.hasVideo) {
            this->mediaInfo.videoCodec.codec = VIDEO_CODEC_VP8;
            if (vCoder.initEncoder(mediaInfo.videoCodec)) {
                ELOG_DEBUG("Error initing encoder");
            }
            this->initVideoPackager();
        }
        if (mediaInfo.hasAudio) {
            ELOG_DEBUG("Init AUDIO processor");
            mediaInfo.audioCodec.codec = AUDIO_CODEC_PCM_U8;
            mediaInfo.audioCodec.sampleRate= 44100;
            mediaInfo.audioCodec.bitRate = 64000;
            encodedAudioBuffer_ = (unsigned char*) malloc(UNPACKAGED_BUFFER_SIZE);
            packagedAudioBuffer_ = (unsigned char*) malloc(UNPACKAGED_BUFFER_SIZE);
            //this->initAudioCoder();
            this->initAudioPackager();
        }

        return 0;
    }

    void OutputProcessor::close(){
        if (audioCoder == 1) {
            avcodec_close(aCoderContext);
            av_free(aCoderContext);
            audioCoder = 0;
        }

        if (videoCoder == 1) {
            vCoder.closeEncoder();
            videoCoder = 0;
        }

        free(encodedBuffer_); encodedBuffer_ = NULL;
        free(packagedBuffer_); packagedBuffer_ = NULL;
        free(rtpBuffer_); rtpBuffer_ = NULL;
        free(encodedAudioBuffer_); encodedAudioBuffer_ = NULL;
        free(packagedAudioBuffer_); packagedAudioBuffer_ = NULL;
    }


    void OutputProcessor::receiveRawData(const RawDataPacket& packet) {
        if (packet.type == VIDEO) {
            ELOG_DEBUG("Encoding video: size %d", packet.length);
            int len = vCoder.encodeVideo(packet.data, packet.length, encodedBuffer_,UNPACKAGED_BUFFER_SIZE);
            if (len > 0)
                this->packageVideo(encodedBuffer_, len, packagedBuffer_);
        } else {
            //      int a = this->encodeAudio(packet.data, packet.length, &pkt);
            //      if (a > 0) {
            //        ELOG_DEBUG("GUAY a %d", a);
            //      }
        }
        //    av_free_packet(&pkt);
    }

    bool OutputProcessor::initAudioCoder() {
        aCoder = avcodec_find_encoder(static_cast<AVCodecID>(mediaInfo.audioCodec.codec));
        if (!aCoder) {
            ELOG_DEBUG("Encoder de audio no encontrado");
            return false;
        }

        aCoderContext = avcodec_alloc_context3(aCoder);
        if (!aCoderContext) {
            ELOG_DEBUG("Error de memoria en coder de audio");
            return false;
        }

        aCoderContext->sample_fmt = AV_SAMPLE_FMT_S16;
        aCoderContext->bit_rate = mediaInfo.audioCodec.bitRate;
        aCoderContext->sample_rate = mediaInfo.audioCodec.sampleRate;
        aCoderContext->channels = 1;

        if (avcodec_open2(aCoderContext, aCoder, NULL) < 0) {
            ELOG_DEBUG("Error al abrir el coder de audio");
            return false;
        }

        audioCoder = 1;
        return true;
    }

    bool OutputProcessor::initAudioPackager() {
        audioPackager = 1;
        audioSeqnum_ = 0;
        return true;
    }

    bool OutputProcessor::initVideoPackager() {
        seqnum_ = 0;
        videoPackager = 1;
        return true;
    }

    int OutputProcessor::calculateAudioLevel(unsigned char* samples, int offset, int length, int overload)
    {
        /*
         * Calculate the root mean square (RMS) of the signal.
         */
        double rms = 0;

        for (; offset < length; offset++)
        {
            double sample = samples[offset];

            sample /= overload;
            rms += sample * sample;
        }

        rms = (length == 0) ? 0 : sqrt(rms / length);

        /*
         * The audio level is a logarithmic measure of the
         * rms level of an audio sample relative to a reference
         * value and is measured in decibels.
         */
        double db;

        /*
         * The minimum&maximum audio level permitted.
         */
        const double MIN_AUDIO_LEVEL = -127;
        const double MAX_AUDIO_LEVEL = 0;

        //ELOG_DEBUG("rms=%f", rms);
        if (rms > 0)
        {
            /*
             * The "zero" reference level is the overload level,
             * which corresponds to 1.0 in this calculation, because
             * the samples are normalized in calculating the RMS.
             */
            db = 20 * log10(rms);

            //ELOG_DEBUG("rms = %f, then db=%f", rms, db);

            //ELOG_DEBUG("db=%f", db);
            /*
             * Ensure that the calculated level is within the minimum
             * and maximum range permitted.
             */
            if (db < MIN_AUDIO_LEVEL)
                db = MIN_AUDIO_LEVEL;
            else if (db > MAX_AUDIO_LEVEL)
                db = MAX_AUDIO_LEVEL;
        }
        else
        {
            db = MIN_AUDIO_LEVEL;
        }

        return (int)round(db);
    }

    static void receiveRtpData(unsigned char*rtpdata, int len) {
        if (audioSink!=NULL){
            static char sendAudioBuffer[1600];

            assert(len<=1600);

            memcpy(sendAudioBuffer, rtpdata, len);
            audioSink->deliverAudioData(sendAudioBuffer, len);
        }

    }

    int OutputProcessor::packageAudio(unsigned char* data, int datalen,
            long int pts) {

        // pcm 20ms  => 160 * 1000 * 1/8000 ( time_base )
        // opus 20ms => 960 * 1000 * 1/48000
        // const int FrameSize = 960;   

        if (audioPackager == 0) {
            ELOG_DEBUG("No se ha inicializado el codec audio RTP!!");
            return -1;
        }

        ELOG_DEBUG("packaugeAudio len %d, pts %ld", datalen, pts);

        unsigned char* inBuff = new unsigned char[datalen + 30];


        //    timeval time;
        //    gettimeofday(&time, NULL);
        //    long millis = (time.tv_sec * 1000) + (time.tv_usec / 1000);

        RtpHeader head;
        head.setSeqNumber(audioSeqnum_++);
        head.setMarker(false);

        head.setExtension(false);
        //head.setExtId(48862); //0xbede profile
        //head.setExtLength(1);       // only 1 ext, Audio level.

        // No need to rescale as already audio time base.
        head.setTimestamp(pts);

        //AudioLevelExtension ext;
        //ext.init();
        //int audioLevel = -calculateAudioLevel(inBuff, 0,FrameSize, 127);

        //ELOG_DEBUG("Calculated audio level=%d", audioLevel);

        //ext.level = audioLevel;
        //uint16_t val = *(reinterpret_cast<uint16_t*>(&ext));
        //val = htonl(val); // don't need it.
        //head.extensions = val;

        //ELOG_DEBUG("head.extensions = %hu", head.extensions);

        // next timestamp will +FrameSize;

        //auto ssrc = videoSink->getAudioSinkSSRC();
        //head.setSSRC(44444);
        //
        auto ssrc = 55543;
        auto mediasource = dynamic_cast<MediaSource*>(rtpReceiver_);
        if (mediasource)
        {
            ssrc = mediasource->getAudioSourceSSRC();
        }
        head.setSSRC(ssrc);
        head.setPayloadType(111);// will be substituted later by client type.

        memcpy(inBuff, &head, head.getHeaderLength());
        memcpy(&inBuff[head.getHeaderLength()], data, datalen);

        receiveRtpData(inBuff, (datalen + head.getHeaderLength()));
        ELOG_DEBUG("Sent %d bytes", datalen + head.getHeaderLength());

        return 1;
    }

    int OutputProcessor::packageVideo(unsigned char* inBuff, int buffSize, unsigned char* outBuff, long int pts) {
        if (videoPackager == 0) {
            ELOG_DEBUG("No se ha inicailizado el codec de output vídeo RTP");
            return -1;
        }

        if (buffSize <= 0) {
            return -1;
        }
        RtpVP8Fragmenter frag(inBuff, buffSize, 1100);
        bool lastFrame = false;
        unsigned int outlen = 0;
        timeval time;
        gettimeofday(&time, NULL);
        long millis = (time.tv_sec * 1000) + (time.tv_usec / 1000);  // NOLINT
        //		timestamp_ += 90000 / mediaInfo.videoCodec.frameRate;
        //int64_t pts = av_rescale(lastPts_, 1000000, (long int)video_time_base_);

        auto ssrc = 55543;
        auto mediasource = dynamic_cast<MediaSource*>(rtpReceiver_);
        if (mediasource)
        {
            ssrc = mediasource->getVideoSourceSSRC();
        }

        do {
            outlen = 0;
            frag.getPacket(outBuff, &outlen, &lastFrame);
            RtpHeader rtpHeader;
            rtpHeader.setMarker(lastFrame?1:0);
            rtpHeader.setSeqNumber(seqnum_++);
            if (pts==0){
                // the input pts is 0, todo:
                rtpHeader.setTimestamp(av_rescale(millis, 90000, 1000)); 
            }else{
                rtpHeader.setTimestamp(av_rescale(pts, 90000, 1000));
            }
            //auto ssrc = videoSink->getVideoSinkSSRC();a

            rtpHeader.setSSRC(ssrc);
            rtpHeader.setPayloadType(100);
            memcpy(rtpBuffer_, &rtpHeader, rtpHeader.getHeaderLength());
            memcpy(&rtpBuffer_[rtpHeader.getHeaderLength()],outBuff, outlen);

            int l = outlen + rtpHeader.getHeaderLength();
            rtpReceiver_->receiveRtpData(rtpBuffer_, l);
        } while (!lastFrame);

        return 0;
    }

    int OutputProcessor::encodeAudio(unsigned char* inBuff, int nSamples, AVPacket* pkt) {
        if (audioCoder == 0) {
            ELOG_DEBUG("No se han inicializado los parámetros del audioCoder");
            return -1;
        }

        AVFrame *frame = av_frame_alloc();
        if (!frame) {
            ELOG_ERROR("could not allocate audio frame");
            return -1;
        }
        int ret, got_output, buffer_size;
        //float t, tincr;

        frame->nb_samples = aCoderContext->frame_size;
        frame->format = aCoderContext->sample_fmt;
        //	frame->channel_layout = aCoderContext->channel_layout;

        /* the codec gives us the frame size, in samples,
         * we calculate the size of the samples buffer in bytes */
        ELOG_DEBUG("channels %d, frame_size %d, sample_fmt %d",
                aCoderContext->channels, aCoderContext->frame_size,
                aCoderContext->sample_fmt);
        buffer_size = av_samples_get_buffer_size(NULL, aCoderContext->channels,
                aCoderContext->frame_size, aCoderContext->sample_fmt, 0);
        uint16_t* samples = reinterpret_cast<uint16_t*>(av_malloc(buffer_size));
        if (!samples) {
            ELOG_ERROR("could not allocate %d bytes for samples buffer",
                    buffer_size);
            return -1;
        }
        /* setup the data pointers in the AVFrame */
        ret = avcodec_fill_audio_frame(frame, aCoderContext->channels,
                aCoderContext->sample_fmt, (const uint8_t*) samples, buffer_size,
                0);
        if (ret < 0) {
            ELOG_ERROR("could not setup audio frame");
            return ret;
        }

        ret = avcodec_encode_audio2(aCoderContext, pkt, frame, &got_output);
        if (ret < 0) {
            ELOG_ERROR("error encoding audio frame");
            return ret;
        }
        if (got_output) {
            //fwrite(pkt.data, 1, pkt.size, f);
            ELOG_DEBUG("Got OUTPUT");
        }
        return ret;
    }
}  // namespace erizo
