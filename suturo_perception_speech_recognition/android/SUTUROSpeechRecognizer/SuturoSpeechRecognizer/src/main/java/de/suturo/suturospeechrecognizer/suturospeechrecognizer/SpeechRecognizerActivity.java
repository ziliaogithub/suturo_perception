package de.suturo.suturospeechrecognizer.suturospeechrecognizer;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.os.Bundle;
import android.speech.RecognizerIntent;
import android.util.Log;
import android.view.KeyEvent;
import android.view.Menu;
import android.view.MenuItem;
import android.view.View;
import android.view.inputmethod.InputMethodManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.ImageView;
import android.widget.TextView;
import android.widget.Toast;

import java.net.MalformedURLException;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.lang.String;


public class SpeechRecognizerActivity extends Activity implements OnTaskCompleted {

    private static final int VOICE_RECOGNITION_REQUEST_CODE = 1001;

    private Button btnSpeak;
    private TextView textMatchesTextView;
    private EditText IPEditText;
    private ImageView statusImageView;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_speech_recognizer);
        btnSpeak = (Button) findViewById(R.id.btSpeak);
        textMatchesTextView = (TextView) findViewById(R.id.textMatcheTextView);
        IPEditText = (EditText) findViewById(R.id.IPeditText);
        statusImageView = (ImageView) findViewById(R.id.statusImageView);

        checkVoiceRecognitionAvailability();
        IPEditText.setOnEditorActionListener(new TextView.OnEditorActionListener() {

            @Override
            public boolean onEditorAction(TextView v, int actionId, KeyEvent event) {
                if (event != null && (event.getKeyCode() == KeyEvent.KEYCODE_ENTER)) {
                    InputMethodManager in = (InputMethodManager) getSystemService(Context.INPUT_METHOD_SERVICE);

                    // NOTE: In the author's example, he uses an identifier
                    // called searchBar. If setting this code on your EditText
                    // then use v.getWindowToken() as a reference to your
                    // EditText is passed into this callback as a TextView

                    in.hideSoftInputFromWindow(v.getWindowToken(),
                            InputMethodManager.HIDE_NOT_ALWAYS
                    );
                    // Must return true here to consume event
                    return true;

                }
                return false;
            }
        });
    }

    public void checkVoiceRecognitionAvailability() {
        PackageManager pm = getPackageManager();
        List<ResolveInfo> activities;
        assert pm != null;
        activities = pm.queryIntentActivities(
                new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH), 0);
        if (activities.size() == 0) {
            btnSpeak.setEnabled(false);
            btnSpeak.setText("Voice recognizer not available");
            Toast.makeText(this, "Voice recognizer not available",
                    Toast.LENGTH_SHORT).show();
        }
    }

    public void speak(View view) {
        Intent intent = new Intent(RecognizerIntent.ACTION_RECOGNIZE_SPEECH);

        // put some extras for activityForResult
        intent.putExtra(RecognizerIntent.EXTRA_CALLING_PACKAGE, getClass()
                .getPackage().getName());
        intent.putExtra(RecognizerIntent.EXTRA_PROMPT, "e.G: cleanup object corny");
        // we want english
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_PREFERENCE, "en-US");
        // Don't use language model 'web search'
        intent.putExtra(RecognizerIntent.EXTRA_LANGUAGE_MODEL,
                RecognizerIntent.LANGUAGE_MODEL_FREE_FORM);

        // Just get the result with the highest confidence
        intent.putExtra(RecognizerIntent.EXTRA_MAX_RESULTS, 1);
        startActivityForResult(intent, VOICE_RECOGNITION_REQUEST_CODE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == VOICE_RECOGNITION_REQUEST_CODE && resultCode == RESULT_OK) {
            ArrayList<String> textMatchList = data
                    .getStringArrayListExtra(RecognizerIntent.EXTRA_RESULTS);

            assert textMatchList != null; // googles recognizer takes care of this
            if (!textMatchList.isEmpty()) {
                // populate the Matches
                textMatchesTextView.setText(textMatchList.get(0));

            // catch a few errors
            } else if (resultCode == RecognizerIntent.RESULT_AUDIO_ERROR) {
                Toast.makeText(this, "Audio Error", Toast.LENGTH_SHORT).show();
            } else if (resultCode == RecognizerIntent.RESULT_CLIENT_ERROR) {
                Toast.makeText(this, "Client Error", Toast.LENGTH_SHORT).show();
            } else if (resultCode == RecognizerIntent.RESULT_NETWORK_ERROR) {
                Toast.makeText(this, "Network Error", Toast.LENGTH_SHORT).show();
            } else if (resultCode == RecognizerIntent.RESULT_NO_MATCH) {
                Toast.makeText(this, "No Match", Toast.LENGTH_SHORT).show();
            } else if (resultCode == RecognizerIntent.RESULT_SERVER_ERROR) {
                Toast.makeText(this, "Server Error", Toast.LENGTH_SHORT).show();
            }
        }
        // TODO: start the async task to post the recognized result here
        super.onActivityResult(requestCode, resultCode, data);
    }

    public void sendCommand(View view) {
        String[] ipAddrPort = IPEditText.getText().toString().split(":");
        try {
            Log.v("debug", "ipAddr: " + ipAddrPort[0]);
            Log.v("debug", "port: "   + ipAddrPort[1]);
        } catch (ArrayIndexOutOfBoundsException e)
        {
            Toast.makeText(this,"Not a valid ip:port combination",Toast.LENGTH_SHORT).show();
            return;
        }

        String ipAddr = ipAddrPort[0];
        int portNumber = Integer.parseInt(ipAddrPort[1]);
        SendCommandTask sender = new SendCommandTask(this);
        String requestString = "/" + textMatchesTextView.getText().toString().replace(" ", "_").toLowerCase();
        Log.v("debug", "requestString: " + requestString);
        try {
            sender.execute(new URL("http", ipAddr, portNumber, requestString));
        } catch (MalformedURLException e) {
            e.printStackTrace();
        }
    }

    @Override
    public boolean onCreateOptionsMenu(Menu menu) {
        
        // Inflate the menu; this adds items to the action bar if it is present.
        getMenuInflater().inflate(R.menu.speech_recognizer, menu);
        return true;
    }

    @Override
    public boolean onOptionsItemSelected(MenuItem item) {
        // Handle action bar item clicks here. The action bar will
        // automatically handle clicks on the Home/Up button, so long
        // as you specify a parent activity in AndroidManifest.xml.
        int id = item.getItemId();
        if (id == R.id.action_settings) {
            return true;
        }
        return super.onOptionsItemSelected(item);
    }

    @Override
    public void onTaskCompleted(Boolean result) {
        if(result == Boolean.TRUE) {
            Log.v("debug", "HTTP Task completed! Text recognized by rosnode.");
            Toast.makeText(this, "Text recognized by node!", Toast.LENGTH_SHORT).show();
            statusImageView.setImageResource(R.drawable.checked_green_256);
        }
        else {
            Log.v("debug", "HTTP Task completed! Text NOT recognized by rosnode!");
            Toast.makeText(this, "Text NOT recognized by node!", Toast.LENGTH_SHORT).show();
            statusImageView.setImageResource(R.drawable.checked_red_256);
        }

    }
}

