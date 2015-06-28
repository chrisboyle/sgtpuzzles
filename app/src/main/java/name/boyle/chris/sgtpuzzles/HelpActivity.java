package name.boyle.chris.sgtpuzzles;

import android.content.Intent;
import android.content.res.Resources;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.support.v7.app.ActionBarActivity;
import android.os.Bundle;
import android.support.v7.app.AppCompatActivity;
import android.view.KeyEvent;
import android.view.MenuItem;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;

import java.io.IOException;
import java.text.MessageFormat;
import java.util.regex.Pattern;


public class HelpActivity extends AppCompatActivity {

	static final String TOPIC = "name.boyle.chris.sgtpuzzles.TOPIC";
	static final String NIGHT = "name.boyle.chris.sgtpuzzles.NIGHT";
	private static final Pattern ALLOWED_TOPICS = Pattern.compile("^[a-z]+$");
	private static final Pattern URL_SCHEME = Pattern.compile("^[a-z0-9]+:");
	private WebView webView;
	private boolean isNight = false;

	@Override
	protected void onCreate(Bundle savedInstanceState) {
		super.onCreate(savedInstanceState);
		Intent intent = getIntent();
		String topic = intent.getStringExtra(TOPIC);
		isNight = intent.getBooleanExtra(NIGHT, false);
		if (!ALLOWED_TOPICS.matcher(topic).matches()) {
			finish();
			return;
		}
		getSupportActionBar().setDisplayHomeAsUpEnabled(true);
		setContentView(R.layout.activity_help);
		webView = (WebView) findViewById(R.id.webview);
		if (isNight) webView.setBackgroundColor(Color.BLACK);
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
		webView.setWebViewClient(new WebViewClient() {
			private String lastStyledUrl = "";

			@Override
			public boolean shouldOverrideUrlLoading(WebView view, String url) {
				if (url.startsWith("file:") || !URL_SCHEME.matcher(url).find()) {
					lastStyledUrl = url;
					loadWithStyle(view, url);
					return true;
				}
				// spawn other app
				startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
				return true;
			}

			// goBack() doesn't call shouldOverrideUrlLoading() :-(
			@Override
			public void onPageFinished(WebView view, String url) {
				if (!lastStyledUrl.equals(url)) {
					lastStyledUrl = url;
					loadWithStyle(view, url);
				}
			}
		});
		final WebSettings settings = webView.getSettings();
		settings.setJavaScriptEnabled(false);  // default, but just to be sure
		settings.setAllowFileAccess(false);  // android_asset still works
		settings.setBlockNetworkImage(true);
		settings.setBuiltInZoomControls(true);
		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.FROYO) {
			settings.setBlockNetworkLoads(true);
			if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.HONEYCOMB) {
				settings.setDisplayZoomControls(false);
				settings.setAllowContentAccess(false);
			}
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
		loadWithStyle(webView, "file:///android_asset/" + assetPath);
	}

	private void loadWithStyle(WebView view, String url) {
		try {
			String data = Utils.readAllOf(getAssets().open(url.replace("file:///android_asset/", "").replaceFirst("#.*", "")));
			if (isNight) {
				data = data.replace("</head>", "<style type=\"text/css\">" + getResources().getString(R.string.css_night) + "</style></head>");
			}
			view.loadDataWithBaseURL(url, data, "text/html", "UTF-8", url);
		} catch (IOException e) {
			throw new RuntimeException(e);
		}
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
