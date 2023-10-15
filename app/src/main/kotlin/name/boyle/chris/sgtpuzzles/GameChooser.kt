package name.boyle.chris.sgtpuzzles

import android.animation.LayoutTransition
import android.annotation.SuppressLint
import android.content.Intent
import android.content.SharedPreferences
import android.content.SharedPreferences.OnSharedPreferenceChangeListener
import android.content.res.Configuration
import android.graphics.Rect
import android.net.Uri
import android.os.Bundle
import android.text.Spannable
import android.text.SpannableStringBuilder
import android.text.style.TextAppearanceSpan
import android.view.Gravity
import android.view.Menu
import android.view.MenuItem
import android.view.MotionEvent
import android.view.View
import android.view.View.GONE
import android.view.View.VISIBLE
import androidx.gridlayout.widget.GridLayout
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.Utils.sendFeedbackDialog
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.BackendName.Companion.byLowerCase
import name.boyle.chris.sgtpuzzles.backend.GUESS
import name.boyle.chris.sgtpuzzles.backend.KEEN
import name.boyle.chris.sgtpuzzles.backend.LIGHTUP
import name.boyle.chris.sgtpuzzles.backend.NET
import name.boyle.chris.sgtpuzzles.backend.SIGNPOST
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.TOWERS
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.CHOOSER_STYLE_KEY
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.SAVED_BACKEND
import name.boyle.chris.sgtpuzzles.config.PrefsConstants.STATE_PREFS_NAME
import name.boyle.chris.sgtpuzzles.databinding.ChooserBinding
import name.boyle.chris.sgtpuzzles.databinding.ListItemBinding
import name.boyle.chris.sgtpuzzles.launch.GameGenerator.Companion.executableIsMissing
import kotlin.math.floor
import kotlin.math.roundToInt

class GameChooser : ActivityWithLoadButton(), OnSharedPreferenceChangeListener {
    private lateinit var binding: ChooserBinding
    private lateinit var prefs: SharedPreferences
    private var useGrid = false
    private val itemBindings: MutableMap<BackendName, ListItemBinding> = LinkedHashMap()
    private var menu: Menu? = null
    private var scrollToOnNextLayout: BackendName? = null
    private var resumeTime: Long = 0
    private var wasNight = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        if (executableIsMissing(this)) {
            finish()
            return
        }
        prefs = PreferenceManager.getDefaultSharedPreferences(this)
        prefs.registerOnSharedPreferenceChangeListener(this)
        wasNight = NightModeHelper.isNight(resources.configuration)
        val state = getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE)
        val oldCS = state.getString(CHOOSER_STYLE_KEY, null)
        if (oldCS != null) {  // migrate to somewhere more sensible
            prefs.edit()
                .putString(CHOOSER_STYLE_KEY, oldCS)
                .apply()
            state.edit()
                .remove(CHOOSER_STYLE_KEY)
                .apply()
        }
        useGrid = prefs.getString(CHOOSER_STYLE_KEY, "list") == "grid"
        binding = ChooserBinding.inflate(layoutInflater)
        setContentView(binding.root)
        buildViews()
        rethinkActionBarCapacity()
        supportActionBar?.addOnMenuVisibilityListener { visible: Boolean ->
            // https://issuetracker.google.com/issues/36994881
            if (!visible) invalidateOptionsMenu()
        }
        binding.scrollView.viewTreeObserver.addOnGlobalLayoutListener {
            if (scrollToOnNextLayout != null) {
                val v: View = itemBindings[scrollToOnNextLayout]!!.root
                binding.scrollView.requestChildRectangleOnScreen(
                    v,
                    Rect(0, 0, v.width, v.height),
                    true
                )
                scrollToOnNextLayout = null
            }
        }
        enableTableAnimations()
    }

    override fun onResume() {
        super.onResume()
        resumeTime = System.nanoTime()
        var currentBackend: BackendName? = null
        val state = getSharedPreferences(STATE_PREFS_NAME, MODE_PRIVATE)
        if (state.contains(SAVED_BACKEND)) {
            currentBackend = byLowerCase(state.getString(SAVED_BACKEND, null))
        }
        BackendName.all.forEach {
            val isCurrent = it === currentBackend
            itemBindings[it]!!.root.isActivated = isCurrent
            if (isCurrent) {
                // wait until we know the size
                scrollToOnNextLayout = it
            }
        }
    }

    private fun enableTableAnimations() {
        binding.table.layoutTransition = LayoutTransition().apply {
            enableTransitionType(LayoutTransition.CHANGING)
        }
    }

    private fun buildViews() {
        itemBindings.clear()
        BackendName.all.forEach { backend ->
            with (ListItemBinding.inflate(layoutInflater)) {
                itemBindings[backend] = this
                icon.setImageDrawable(backend.icon(this@GameChooser))
                text.text = SpannableStringBuilder(backend.displayName).apply {
                    setSpan(
                        TextAppearanceSpan(this@GameChooser, R.style.ChooserItemName),
                        0, length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
                    )
                    append(": ")
                    append(getString(backend.description))
                }
                text.visibility = if (useGrid) GONE else VISIBLE
                with (root) {
                    ignoreTouchAfterResume()
                    setOnClickListener {
                        startActivity(Intent(this@GameChooser, GamePlay::class.java).apply {
                            data = Uri.fromParts(GamePlay.OUR_SCHEME, backend.toString(), null)
                        })
                    }
                    setOnLongClickListener {
                        toggleStarred(backend)
                        true
                    }
                    isFocusable = true
                    layoutParams = mkLayoutParams()
                }
                binding.table.addView(root)
            }
        }
        rethinkColumns(true)
    }

    @SuppressLint("ClickableViewAccessibility") // Does not define a new click mechanism
    private fun View.ignoreTouchAfterResume() {
        setOnTouchListener { _: View?, _: MotionEvent? -> System.nanoTime() - resumeTime < 300000000 }
    }

    private fun mkLayoutParams(): GridLayout.LayoutParams =
        GridLayout.LayoutParams().apply { setGravity(Gravity.CENTER_HORIZONTAL) }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        val isNight = NightModeHelper.isNight(resources.configuration)
        if (wasNight != isNight) {
            BackendName.all.forEach {
                itemBindings[it]!!.icon.setImageDrawable(it.icon(this))
            }
        }
        rethinkColumns(wasNight != isNight)
        menu?.let {
            // https://github.com/chrisboyle/sgtpuzzles/issues/227
            it.clear()
            onCreateOptionsMenu(it)
        }
        rethinkActionBarCapacity()
        wasNight = isNight
    }

    private var columns = 0
    private var colWidthPx = 0
    private fun rethinkColumns(force: Boolean) {
        val dm = resources.displayMetrics
        val colWidthDipNeeded = if (useGrid) 72 else 298
        val screenWidthDip = dm.widthPixels.toDouble() / dm.density
        val newColumns = floor(screenWidthDip / colWidthDipNeeded).toInt().coerceAtLeast(1)
        val newColWidthPx = floor(dm.widthPixels.toDouble() / newColumns).toInt()
        if (force || columns != newColumns || colWidthPx != newColWidthPx) {
            columns = newColumns
            colWidthPx = newColWidthPx
            val (starred, others) = BackendName.all.partition { isStarred(it) }
            val anyStarred = starred.isNotEmpty()
            binding.gamesStarred.visibility = if (anyStarred) VISIBLE else GONE
            var row = 0
            if (anyStarred) {
                setGridCells(binding.gamesStarred, 0, row++, columns)
                row = setViewsGridCells(row, starred.map {itemBindings[it]!!}, true)
            }
            binding.gamesOthers.setText(if (anyStarred) R.string.games_others else R.string.games_others_none_starred)
            setGridCells(binding.gamesOthers, 0, row++, columns)
            setViewsGridCells(row, others.map {itemBindings[it]!!}, false)
        }
    }

    private fun setGridCells(v: View, x: Int, y: Int, w: Int) {
        v.layoutParams = (v.layoutParams as GridLayout.LayoutParams).apply {
            width = colWidthPx * w
            columnSpec = GridLayout.spec(x, w, GridLayout.START)
            rowSpec = GridLayout.spec(y, 1, GridLayout.START)
            setGravity(if (useGrid && w == 1) Gravity.CENTER_HORIZONTAL else Gravity.START)
        }
    }

    private fun setViewsGridCells(
        startRow: Int,
        itemBindings: List<ListItemBinding>,
        starred: Boolean
    ): Int {
        var col = 0
        var row = startRow
        itemBindings.forEach {
            it.star.visibility = if (starred) VISIBLE else GONE
            if (col >= columns) {
                col = 0
                row++
            }
            setGridCells(it.root, col++, row, 1)
        }
        row++
        return row
    }

    private fun isStarred(game: BackendName): Boolean =
        prefs.getBoolean("starred_$game", DEFAULT_STARRED.contains(game))

    private fun toggleStarred(game: BackendName) {
        prefs.edit().putBoolean("starred_$game", !isStarred(game)).apply()
        rethinkColumns(true)
    }

    private fun rethinkActionBarCapacity() {
        menu?.apply {
            val dm = resources.displayMetrics
            val screenWidthDIP = (dm.widthPixels.toDouble() / dm.density).roundToInt()
            val state =
                if (screenWidthDIP >= 480) (MenuItem.SHOW_AS_ACTION_ALWAYS or MenuItem.SHOW_AS_ACTION_WITH_TEXT) else MenuItem.SHOW_AS_ACTION_ALWAYS
            for (i in listOf(R.id.load, R.id.settings, R.id.help_menu)) {
                findItem(i).setShowAsAction(state)
            }
        }
    }

    override fun onCreateOptionsMenu(menu: Menu): Boolean {
        super.onCreateOptionsMenu(menu)
        this.menu = menu
        menuInflater.inflate(R.menu.chooser, menu)
        rethinkActionBarCapacity()
        return true
    }

    override fun onPrepareOptionsMenu(menu: Menu): Boolean {
        super.onPrepareOptionsMenu(menu)
        return true
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        var ret = true
        when (item.itemId) {
            R.id.settings -> startActivity(Intent(this, PrefsActivity::class.java))
            R.id.load -> loadGame()
            R.id.contents -> startActivity(Intent(this, HelpActivity::class.java).apply {
                putExtra(HelpActivity.TOPIC, "index")
            })
            R.id.feedback -> sendFeedbackDialog(this)
            else -> ret = super.onOptionsItemSelected(item)
        }
        // https://issuetracker.google.com/issues/36994881
        invalidateOptionsMenu()
        return ret
    }

    override fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String?) {
        if (key != CHOOSER_STYLE_KEY) return
        val newGrid = "grid" == prefs.getString(CHOOSER_STYLE_KEY, "list")
        if (useGrid == newGrid) return
        useGrid = newGrid
        rethinkActionBarCapacity()
        itemBindings.values.forEach { it.text.visibility = if (useGrid) GONE else VISIBLE }
        rethinkColumns(true)
    }

    companion object {

        private val DEFAULT_STARRED = setOf(GUESS, KEEN, LIGHTUP, NET, SIGNPOST, SOLO, TOWERS)

    }
}