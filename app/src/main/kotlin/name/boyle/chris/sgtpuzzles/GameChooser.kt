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
import androidx.gridlayout.widget.GridLayout
import androidx.preference.PreferenceManager
import name.boyle.chris.sgtpuzzles.Utils.sendFeedbackDialog
import name.boyle.chris.sgtpuzzles.backend.BackendName
import name.boyle.chris.sgtpuzzles.backend.BackendName.Companion.all
import name.boyle.chris.sgtpuzzles.backend.BackendName.Companion.byLowerCase
import name.boyle.chris.sgtpuzzles.backend.GUESS
import name.boyle.chris.sgtpuzzles.backend.KEEN
import name.boyle.chris.sgtpuzzles.backend.LIGHTUP
import name.boyle.chris.sgtpuzzles.backend.NET
import name.boyle.chris.sgtpuzzles.backend.SIGNPOST
import name.boyle.chris.sgtpuzzles.backend.SOLO
import name.boyle.chris.sgtpuzzles.backend.TOWERS
import name.boyle.chris.sgtpuzzles.config.PrefsConstants
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
        val state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE)
        val oldCS = state.getString(PrefsConstants.CHOOSER_STYLE_KEY, null)
        if (oldCS != null) {  // migrate to somewhere more sensible
            prefs.edit()
                .putString(PrefsConstants.CHOOSER_STYLE_KEY, oldCS)
                .apply()
            state.edit()
                .remove(PrefsConstants.CHOOSER_STYLE_KEY)
                .apply()
        }
        useGrid = prefs.getString(PrefsConstants.CHOOSER_STYLE_KEY, "list") == "grid"
        binding = ChooserBinding.inflate(layoutInflater)
        setContentView(binding.root)
        buildViews()
        rethinkActionBarCapacity()
        supportActionBar?.addOnMenuVisibilityListener { visible: Boolean ->
            // https://issuetracker.google.com/issues/36994881
            @Suppress("DEPRECATION")
            if (!visible) supportInvalidateOptionsMenu()
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
        val state = getSharedPreferences(PrefsConstants.STATE_PREFS_NAME, MODE_PRIVATE)
        if (state.contains(PrefsConstants.SAVED_BACKEND)) {
            currentBackend = byLowerCase(state.getString(PrefsConstants.SAVED_BACKEND, null))
        }
        for (backend in all) {
            val isCurrent = backend === currentBackend
            itemBindings[backend]!!.root.isActivated = isCurrent
            if (isCurrent) {
                // wait until we know the size
                scrollToOnNextLayout = backend
            }
        }
    }

    private fun enableTableAnimations() {
        val transition = LayoutTransition()
        transition.enableTransitionType(LayoutTransition.CHANGING)
        binding.table.layoutTransition = transition
    }

    private fun buildViews() {
        itemBindings.clear()
        for (backend in all) {
            with (ListItemBinding.inflate(layoutInflater)) {
                itemBindings[backend] = this
                icon.setImageDrawable(backend.icon(this@GameChooser))
                val desc = SpannableStringBuilder(backend.displayName)
                desc.setSpan(
                    TextAppearanceSpan(this@GameChooser, R.style.ChooserItemName),
                    0, desc.length, Spannable.SPAN_EXCLUSIVE_EXCLUSIVE
                )
                desc.append(": ").append(getString(backend.description))
                text.text = desc
                text.visibility = if (useGrid) View.GONE else View.VISIBLE
                ignoreTouchAfterResume(root)
                root.setOnClickListener {
                    startActivity(Intent(this@GameChooser, GamePlay::class.java).apply {
                        data = Uri.fromParts(GamePlay.OUR_SCHEME, backend.toString(), null)
                    })
                    overridePendingTransition(0, 0)
                }
                root.setOnLongClickListener {
                    toggleStarred(backend)
                    true
                }
                root.isFocusable = true
                root.layoutParams = mkLayoutParams()
                binding.table.addView(root)
            }
        }
        rethinkColumns(true)
    }

    @SuppressLint("ClickableViewAccessibility") // Does not define a new click mechanism
    private fun ignoreTouchAfterResume(view: View) {
        view.setOnTouchListener { _: View?, _: MotionEvent? -> System.nanoTime() - resumeTime < 300000000 }
    }

    private fun mkLayoutParams(): GridLayout.LayoutParams =
        GridLayout.LayoutParams().apply { setGravity(Gravity.CENTER_HORIZONTAL) }

    override fun onConfigurationChanged(newConfig: Configuration) {
        super.onConfigurationChanged(newConfig)
        val isNight = NightModeHelper.isNight(resources.configuration)
        if (wasNight != isNight) {
            for (backend in all) {
                itemBindings[backend]!!.icon.setImageDrawable(backend.icon(this))
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
            val (starred, others) = all.partition { isStarred(it) }
            val anyStarred = starred.isNotEmpty()
            binding.gamesStarred.visibility = if (anyStarred) View.VISIBLE else View.GONE
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
        for (itemBinding in itemBindings) {
            itemBinding.star.visibility = if (starred) View.VISIBLE else View.GONE
            if (col >= columns) {
                col = 0
                row++
            }
            setGridCells(itemBinding.root, col++, row, 1)
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
            var state = MenuItem.SHOW_AS_ACTION_ALWAYS
            if (screenWidthDIP >= 480) {
                state = state or MenuItem.SHOW_AS_ACTION_WITH_TEXT
            }
            findItem(R.id.settings).setShowAsAction(state)
            findItem(R.id.load).setShowAsAction(state)
            findItem(R.id.help_menu).setShowAsAction(state)
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
            R.id.settings -> {
                startActivity(Intent(this, PrefsActivity::class.java))
            }
            R.id.load -> {
                loadGame()
            }
            R.id.contents -> {
                val intent = Intent(this, HelpActivity::class.java)
                intent.putExtra(HelpActivity.TOPIC, "index")
                startActivity(intent)
            }
            R.id.feedback -> {
                sendFeedbackDialog(this)
            }
            else -> {
                ret = super.onOptionsItemSelected(item)
            }
        }
        // https://issuetracker.google.com/issues/36994881
        @Suppress("DEPRECATION")
        supportInvalidateOptionsMenu()
        return ret
    }

    override fun onSharedPreferenceChanged(sharedPreferences: SharedPreferences, key: String) {
        if (key != PrefsConstants.CHOOSER_STYLE_KEY) return
        val newGrid = "grid" == prefs.getString(PrefsConstants.CHOOSER_STYLE_KEY, "list")
        if (useGrid == newGrid) return
        useGrid = newGrid
        rethinkActionBarCapacity()
        for (itemBinding in itemBindings.values) {
            itemBinding.text.visibility = if (useGrid) View.GONE else View.VISIBLE
        }
        rethinkColumns(true)
    }

    companion object {

        private val DEFAULT_STARRED = setOf(GUESS, KEEN, LIGHTUP, NET, SIGNPOST, SOLO, TOWERS)

    }
}