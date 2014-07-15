/*
  Offscreen Android Views library for Qt

  Author:
  Sergey A. Galin <sergey.galin@gmail.com>

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

package ru.dublgis.androidhelpers;

import java.lang.Thread;
import java.util.concurrent.Semaphore;
import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.content.pm.ActivityInfo;
import android.content.DialogInterface;
import android.util.Log;
import android.view.View;
import android.os.Looper;

public class DialogHelper
{
    public static final String TAG = "Grym/DialogHelper";
    private long native_ptr_ = 0;
    private Semaphore semaphore_ = new Semaphore(1);

    public DialogHelper(long native_ptr)
    {
        Log.i(TAG, "DialogHelper constructor");
        native_ptr_ = native_ptr;
    }

    //! Called from C++ to notify us that the associated C++ object is being destroyed.
    public void cppDestroyed()
    {
        synchronized(this)
        {
            native_ptr_ = 0;
        }
    }

    public void showMessage(final String title, final String explanation, final String positiveButtonText,
        final String negativeButtonText, final String neutralButtonText, final boolean loop, final int lockOrientation)
    {
        final Activity a = getActivity();
        final boolean ui_thread = Thread.currentThread().getId() == a.getMainLooper().getThread().getId();
        Runnable runnable = new Runnable(){
            @Override
            public void run()
            {
                int orientation = -1;
                try
                {
                    if (lockOrientation != -1)
                    {
                        orientation = a.getRequestedOrientation();
                        a.setRequestedOrientation(ActivityInfo.SCREEN_ORIENTATION_NOSENSOR);
                    }
                }
                catch(Exception e)
                {
                    Log.e(TAG, "showMessage: exception (1): "+e);
                }
                final int restore_orientation = orientation;
                AlertDialog.Builder builder = new AlertDialog.Builder(getActivity());
                builder.setTitle(title);
                builder.setMessage(explanation);

                DialogInterface.OnClickListener listener = new DialogInterface.OnClickListener() {
                    @Override
                    public void onClick(DialogInterface dialog, int whichButton)
                    {
                        try
                        {
                            if (lockOrientation != -1)
                            {
                                a.setRequestedOrientation(restore_orientation);
                            }
                        }
                        catch(Exception e)
                        {
                            Log.e(TAG, "showMessage: exception (2): "+e);
                        }
                        synchronized(this)
                        {
                            showMessageCallback(native_ptr_, whichButton);
                        }
                        if (loop && !ui_thread)
                        {
                            semaphore_.release();
                        }
                    }
                };

                DialogInterface.OnCancelListener cancel_listener = new DialogInterface.OnCancelListener() {
                    @Override
                    public void onCancel(DialogInterface dialog)
                    {
                        try
                        {
                            if (lockOrientation != -1)
                            {
                                a.setRequestedOrientation(restore_orientation);
                            }
                        }
                        catch(Exception e)
                        {
                            Log.e(TAG, "showMessage: exception (2): "+e);
                        }
                        synchronized(this)
                        {
                            showMessageCallback(native_ptr_, 0);
                        }
                        if (loop && !ui_thread)
                        {
                            semaphore_.release();
                        }
                    }
                };

                if (positiveButtonText != null && !positiveButtonText.isEmpty())
                {
                    builder.setPositiveButton(positiveButtonText, listener);
                }
                if (negativeButtonText != null && !negativeButtonText.isEmpty())
                {
                    builder.setNegativeButton(negativeButtonText, listener);
                }
                if (neutralButtonText != null && !neutralButtonText.isEmpty())
                {
                    builder.setNeutralButton(neutralButtonText, listener);
                }
                builder.setOnCancelListener(cancel_listener);

                builder.show();

                if (loop && ui_thread)
                {
                    Log.i(TAG, "Waiting for the dialog in UI thread.");
                    Looper.loop();
                }
            }
        };
        if (!ui_thread)
        {
            Log.i(TAG, "Non-UI thread mode.");
            try
            {
                if (loop)
                {
                    semaphore_.acquire();
                }
                a.runOnUiThread(runnable);
                if (loop)
                {
                    semaphore_.acquire();
                    semaphore_.release();
                }
            }
            catch(Exception e)
            {
                Log.e(TAG, "showMessage: exception (3): "+e);
            }
        }
        else // ui_thread
        {
            Log.i(TAG, "UI thread mode.");
            runnable.run();
        }
    }

    public native Activity getActivity();
    public native void showMessageCallback(long nativeptr, int button);
}
