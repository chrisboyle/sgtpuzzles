// Glue connecting Puzzles to the KaiAds API.
//
// The Kai Store requires that we support advertisements through
// KaiAds.  To avoid polluting the Puzzles core with this, the
// relevant code is largely confined to this file.  It can then be
// included in builds that are destined for the Kai Store and left out
// of others.  The main puzzle code, and the KaiAds API (as supplied
// by Kai Technologies) should be loaded before this file.


(function() {
    // To run, we need to be on KaiOS with the KaiAds SDK and the
    // Open Web Apps API.
    if (window.getKaiAd === undefined || navigator.mozApps === undefined ||
        navigator.userAgent.toLowerCase().indexOf('kaios') == -1) return;
    // If those prerequisites are satisfied, install the button.
    var advertbutton = document.createElement("button");
    advertbutton.type = "button";
    advertbutton.textContent = "Display an advert...";
    advertbutton.disabled = true;
    var advertli = document.createElement("li");
    advertli.appendChild(advertbutton);
    var topmenu = menuform.querySelector("ul");
    var before = topmenu.querySelector(":scope > li:nth-last-child(2)");
    topmenu.insertBefore(advertli, before);
    // Now work out whether we're installed from the Store (and hence
    // want real adverts) or not (and hence want test ones).
    var selfrequest = navigator.mozApps.getSelf();
    selfrequest.onerror = function() {
        console.log("Error getting own app record: ", selfrequest.error.name);
        // Leave the button disabled.
    };
    selfrequest.onsuccess = function() {
        var testmode = selfrequest.result.installOrigin !=
            "app://kaios-plus.kaiostech.com";

        advertbutton.addEventListener("click", function(e) {
            // The KaiAds SDK provides this function.
            getKaiAd({
                publisher: 'dac9c115-ec42-4175-ac5e-47e118cc541b',
                test: testmode ? 1 : 0,
                timeout: 10000,
                onready: function(ad) {
                    ad.on('close', function () {
                        // KaiAds adds inline styles to the body and doesn't
                        // remove them, so we do it ourselves.
                        document.body.style = '';
                        onscreen_canvas.focus();
                    });
                    ad.call('display');
                },
                onerror: function(err) {
                    alert(`Sorry; no advert available (KaiAds error ${err}).`);
                    onscreen_canvas.focus(); // Close the menu.
                }
            });
        });
        advertbutton.disabled = false;
    };
})();
