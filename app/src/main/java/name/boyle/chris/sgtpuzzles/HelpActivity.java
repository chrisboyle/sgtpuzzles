package name.boyle.chris.sgtpuzzles;

import android.content.Intent;
import android.content.res.Resources;
import android.os.Build;
import android.support.v7.app.ActionBarActivity;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.MenuItem;
import android.webkit.WebChromeClient;
import android.webkit.WebView;

import java.io.IOException;
import java.text.MessageFormat;


public class HelpActivity extends ActionBarActivity {

	static final String TOPIC = "name.boyle.chris.sgtpuzzles.TOPIC";
	private WebView webView;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		Intent intent = getIntent();
		String topic = intent.getStringExtra(TOPIC);
		getSupportActionBar().setDisplayHomeAsUpEnabled(true);
		setContentView(R.layout.activity_help);
		webView = (WebView) findViewById(R.id.webview);
		webView.setWebChromeClient(new WebChromeClient() {
			public void onReceivedTitle(WebView w, String title) {
				getSupportActionBar().setTitle(getString(R.string.title_activity_help) + ": " + title);
			}

			// onReceivedTitle doesn't happen on back button :-(
			public void onProgressChanged(WebView w, int progress) {
				if (progress == 100)
					getSupportActionBar().setTitle(getString(R.string.title_activity_help) + ": " + w.getTitle());
			}
		});
		webView.getSettings().setBuiltInZoomControls(true);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
			webView.getSettings().setDisplayZoomControls(false);
		}
		final Resources resources = getResources();
		final String lang = resources.getConfiguration().locale.getLanguage();
		String assetPath = helpPath(lang, topic);
		boolean haveLocalised = false;
		try {
			final String[] list = resources.getAssets().list(lang);
			for (String s : list) {
				if (s.equals(topic + ".html")) {
					haveLocalised = true;
				}
			}
		} catch (IOException ignored) {}
		if (!haveLocalised) {
			assetPath = helpPath("en", topic);
		}
		webView.loadUrl("file:///android_asset/" + assetPath);
	}

	private static String helpPath(String lang, String topic) {
		return MessageFormat.format("{0}/{1}.html", lang, topic);
	}

	@Override
	public boolean onKeyDown(int keyCode, KeyEvent event) {
		if (event.getAction() == KeyEvent.ACTION_DOWN && event.getRepeatCount() == 0
				&& keyCode == KeyEvent.KEYCODE_BACK) {
			if (webView.canGoBack()) {
				webView.goBack();
			} else {
				finish();
			}
			return true;
		}
		return super.onKeyDown(keyCode, event);
	}

	@Override
	public boolean onOptionsItemSelected(MenuItem item) {
		int id = item.getItemId();
		if (id == android.R.id.home) {
			finish();
			return true;
		}
		return super.onOptionsItemSelected(item);
	}
}
