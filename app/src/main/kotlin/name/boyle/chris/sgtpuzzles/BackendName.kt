package name.boyle.chris.sgtpuzzles

import android.content.Context
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.core.content.ContextCompat

sealed class BackendName(val sourceName: String, val displayName: String, @DrawableRes val icon: Int, @StringRes val description: Int, val colours: Set<String>) {
    override fun toString() = sourceName

    fun getIcon(context: Context) = ContextCompat.getDrawable(context, icon)!!

    fun isLatin() = LATIN.contains(this)

    companion object {
        private val ALL: Set<BackendName> by lazy {
            BackendName::class.sealedSubclasses.map { it.objectInstance!! }.toSet()
        }

        @JvmStatic
        fun all() = ALL
        private val BY_DISPLAY_NAME: Map<String, BackendName> by lazy {
            BackendName::class.sealedSubclasses.associate { it.objectInstance!!.displayName to it.objectInstance!! }
        }

        @JvmStatic
        fun byDisplayName(name: String?) = BY_DISPLAY_NAME[name]
        private val BY_LOWERCASE: Map<String, BackendName> by lazy {
            BackendName::class.sealedSubclasses.associate { it.objectInstance!!.sourceName to it.objectInstance!! }
        }

        @JvmStatic
        fun byLowerCase(name: String?) = BY_LOWERCASE[name]

        private val LATIN = setOf(KEEN, SOLO, TOWERS, UNEQUAL)
    }
}
