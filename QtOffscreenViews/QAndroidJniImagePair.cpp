/*
  Offscreen Android Views library for Qt

  Author:
  Sergey A. Galin <sergey.galin@gmail.com>

  Contributor:
  Ivan 'w23' Avdeev <marflon@gmail.com>

  Distrbuted under The BSD License

  Copyright (c) 2014, DoubleGIS, LLC.
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
	this list of conditions and the following disclaimer.
  * Redistributions in binary form must reproduce the above copyright notice,
	this list of conditions and the following disclaimer in the documentation
	and/or other materials provided with the distribution.
  * Neither the name of the DoubleGIS, LLC nor the names of its contributors
	may be used to endorse or promote products derived from this software
	without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
  THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
  THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <unistd.h>
#include <android/bitmap.h>
#include <QDebug>
#include <QColor>
#include "QAndroidJniImagePair.h"

static QImage::Format AndroidBitmapFormat_to_QImageFormat(uint32_t abf)
{
	switch(abf)
	{
	case ANDROID_BITMAP_FORMAT_RGB_565:
		return QImage::Format_RGB16;
	case ANDROID_BITMAP_FORMAT_RGBA_8888:
		return QImage::Format_ARGB32;
	default:
		qCritical()<<"ERROR: Invalid Android bitmap format:"<<abf;
		return QImage::Format_Invalid;
	}
}

static QImage::Format qtImageFormatForBitness(int bitness)
{
	// Now, everything is rewritten so the format mapping happens inside of
	// AndroidBitmapFormat_to_QImageFormat().
	switch(bitness)
	{
		case 32: return AndroidBitmapFormat_to_QImageFormat(ANDROID_BITMAP_FORMAT_RGBA_8888);
		case 16: return AndroidBitmapFormat_to_QImageFormat(ANDROID_BITMAP_FORMAT_RGB_565);
		default:
			qCritical()<<"Invalid image bitness:"<<bitness;
			return AndroidBitmapFormat_to_QImageFormat(ANDROID_BITMAP_FORMAT_RGBA_8888);
	}
}

QAndroidJniImagePair::QAndroidJniImagePair(int bitness)
	: qjniimagepairclass_("ru/dublgis/offscreenview/QJniImagePair", false)
	, mBitmap()
    , mImageOnBitmap()
	, bitness_(bitness)
{
	dispose();
}

QAndroidJniImagePair::~QAndroidJniImagePair()
{
}

void QAndroidJniImagePair::preloadJavaClasses()
{
	QAndroidQPAPluginGap::preloadJavaClass("ru/dublgis/offscreenview/QJniImagePair");
}

void QAndroidJniImagePair::dispose()
{
	if (mImageOnBitmap.width() == 1 && mImageOnBitmap.height() == 1 && !mBitmap)
	{
		return; // Already a dispose
	}
	mImageOnBitmap = QImage(1, 1, qtImageFormatForBitness(bitness_));
	mBitmap.reset();
}

QJniObject * QAndroidJniImagePair::createBitmap(const QSize & size)
{
	if (qjniimagepairclass_.jClass() == 0)
	{
		qCritical()<<"QJniImagePair class is null!";
		return 0;
	}
	try
	{
		return qjniimagepairclass_.callStaticParamObject(
			"createBitmap", "android/graphics/Bitmap", "III",
			jint(size.width()), jint(size.height()), jint(bitness_));
	}
	catch(QJniBaseException & e)
	{
		qCritical()<<"Failed to create bitmap:"<<e.what();
		return 0;
	}
}

bool QAndroidJniImagePair::doResize(const QSize & size)
{
	qDebug()<<"QAndroidJniImagePair::resize (static) tid:"<<gettid()<<"new size:"<<size;

	if (size.width() < 1 || size.height() < 1)
	{
        qCritical()<<__FUNCTION__<<"- Supplied image dimenstions are invalid!";
        return false;
    }

	QJniEnvPtr jep;

    // We'll need a new bitmap for the new size
	QImage::Format format = qtImageFormatForBitness(bitness_);

	// Create new Android bitmap
	QScopedPointer<QJniObject> newBitmap(createBitmap(size));
	if (!newBitmap || newBitmap->jObject() == 0)
    {
		qCritical("Could not create %dx%d bitmap! bitmap=%p, jbitmap=%p",
			size.width(), size.height(), newBitmap.data(), (newBitmap.data())? newBitmap->jObject(): 0);
		dispose();
        return false;
    }

    // Request image format for the bitmap created
    uint32_t bwidth, bheight, bstride;
	AndroidBitmapInfo binfo;
	memset(&binfo, 0, sizeof(binfo)); // Important!
	int get_info_result = AndroidBitmap_getInfo(jep.env(), newBitmap->jObject(), &binfo);
	if (get_info_result != 0)
	{
		// On some bogus devices call to AndroidBitmap_getInfo()
		// may return error code. Zeroing binfo will cause the code below
		// to fall back to calculating it here.
		qWarning()<<"Could not get new surface info, error:"<<get_info_result;
		memset(&binfo, 0, sizeof(binfo));
	}
	bwidth = binfo.width;
	bheight = binfo.height;
	bstride = binfo.stride;
	// binfo.flags is typically 0 and is not used.

	// Here, we have to make a workaround for some sorry devices.
	// For example, Huawei MediaPad returns: stride instead of width, width instead of height,
	// zero instead of stride. In such case we just try to assume that the image format is
	// standard. Worked good so far.
	if (binfo.format == 0 || bstride == 0 || bwidth == 0 || bheight == 0 || bstride < bwidth)
	{
		qCritical()<<"Invalid AndroidBitmapInfo. Will fall back to standard bitmap properties: "
					 "width:"<<bwidth<<"height:"<<bheight
				   <<"stride:"<<bstride<<"format:"<<binfo.format<<"flags:"<<binfo.flags;
		bwidth = size.width();
		bheight = size.height();
		bstride = size.width() * ((bitness_ == 32)? 4: 2);
		format = qtImageFormatForBitness(bitness_);
	}
	else
	{
		format = AndroidBitmapFormat_to_QImageFormat(binfo.format);
		if (format == QImage::Format_Invalid)
		{
			qCritical()<<"Don't know how to create bitmap of this Android bitmap format:"<<binfo.format;
			dispose();
			return false;
		}
	}
	if (uint32_t(size.width()) != bwidth || uint32_t(size.height()) != bheight)
	{
		qWarning()<<"Android bitmap size:"<<bwidth<<"x"<<bheight
				  <<"is different than the requested size:"<<size.width()<<"x"<<size.height();
	}

    qDebug()<<"AndroidBitmapInfo: width:"<<bwidth<<"height:"<<bheight
            <<"stride:"<<bstride<<"QImage::format:"<<static_cast<int>(format);

    //
    // Lock Android bitmap's pixels so we could create a QImage over it
    //
	void * ptr = 0;
	int lock_pixels_result = AndroidBitmap_lockPixels(jep.env(), newBitmap->jObject(), &ptr);
	if (lock_pixels_result != 0)
	{
		qCritical()<<"Could not get new surface pointer, error:"<<lock_pixels_result;
		dispose();
        return false;
    }
	if (!ptr)
	{
        qCritical()<<"Could not get new surface pointer, null pointer returned.";
		dispose();
        return false;
    }

    //
    // Create QImage in the same memory area as the Android bitmap
    //

    // "Constructs an image with the given width, height and format,
    // that uses an existing memory buffer, data. The width and height
    // must be specified in pixels. bytesPerLine specifies the number
    // of bytes per line (stride)."
    qDebug()<<"Constructing QImage buffer:"<<bwidth<<"x"<<bheight
            <<bstride<<static_cast<int>(format);
	mImageOnBitmap = QImage(
		static_cast<uchar*>(ptr),
		bwidth,
		bheight,
		bstride,
		format);
	if (mImageOnBitmap.isNull())
	{
        qCritical()<<"Error: called QImage constructor but got null image! Memory error?";
		dispose();
		return false;
	}

	mBitmap.reset(newBitmap.take());
	return true;
}

void QAndroidJniImagePair::fill(const QColor & color, bool to_android_color)
{
	QColor fill = color;
	if (to_android_color && bitness_ == 32)
	{
		fill = QColor(fill.blue(), fill.green(), fill.red(), fill.alpha());
	}
	mImageOnBitmap.fill(fill);
}

void QAndroidJniImagePair::convert32BitImageFromQtToAndroid()
{
	if (bitness_ == 32)
	{
		quint32 * ptr = reinterpret_cast<quint32*>(mImageOnBitmap.scanLine(0));
		QSize sz = mImageOnBitmap.size();
		int nquads = sz.width() * sz.height();
		for (int i = 0; i < nquads; ++i, ++ptr)
		{
			quint32 c = *ptr;
			// Source: ARGB; formula: R | B00 | A0G0 => ABGR
			*ptr = ((c >> 16) & 0xFF) | ((c & 0xFF) << 16) | (c & 0xFF00FF00);
		}
	}
}

void QAndroidJniImagePair::convert32BitImageFromQtToAndroid(QImage & out_image) const
{
	if (bitness_ == 32)
	{
		if (out_image.size() != mImageOnBitmap.size()
			|| out_image.format() != mImageOnBitmap.format())
		{
			out_image = QImage(mImageOnBitmap.size(), mImageOnBitmap.format());
		}
		const quint32 * src = reinterpret_cast<const quint32*>(mImageOnBitmap.scanLine(0));
		quint32 * dest = reinterpret_cast<quint32*>(out_image.scanLine(0));
		QSize sz = mImageOnBitmap.size();
		int nquads = sz.width() * sz.height();
		for (int i = 0; i < nquads; ++i, ++src, ++dest)
		{
			quint32 c = *src;
			// Source: ARGB; formula: R | B00 | A0G0 => ABGR
			*dest = ((c >> 16) & 0xFF) | ((c & 0xFF) << 16) | (c & 0xFF00FF00);
		}
	}
	else
	{
		out_image = mImageOnBitmap;
	}
}

void QAndroidJniImagePair::convert32BitImageFromAndroidToQt()
{
	// The trick is that the conversion is currently reversible, because
	// it converts ARGB to ABGR by swapping R and B it also does the backward job.
	convert32BitImageFromQtToAndroid();
}

void QAndroidJniImagePair::convert32BitImageFromAndroidToQt(QImage & out_image) const
{
	// The trick is that the conversion is currently reversible, because
	// it converts ARGB to ABGR by swapping R and B it also does the backward job.
	convert32BitImageFromQtToAndroid(out_image);
}

bool QAndroidJniImagePair::isAllocated() const
{
	return mBitmap && mBitmap->jObject() != 0 && !mImageOnBitmap.isNull();
}

bool QAndroidJniImagePair::resize(int w, int h)
{
	if (isAllocated() && mImageOnBitmap.width() == w && mImageOnBitmap.height() == h)
	{
		return true;
	}
	return doResize(QSize(w, h));
}

bool QAndroidJniImagePair::resize(const QSize & sz)
{
	return resize(sz.width(), sz.height());
}

QSize QAndroidJniImagePair::size() const
{
	if (!isAllocated())
	{
		return QSize(0, 0);
	}
	return mImageOnBitmap.size();
}


