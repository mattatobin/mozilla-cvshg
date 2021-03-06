// Load in the test harness
var scriptLoader = Components.classes["@mozilla.org/moz/jssubscript-loader;1"]
                             .getService(Components.interfaces.mozIJSSubScriptLoader);
scriptLoader.loadSubScript("chrome://mochikit/content/browser/xpinstall/tests/harness.js", this);

// ----------------------------------------------------------------------------
// Tests installing an unsigned add-on through a navigation. Should not be
// blocked since the referer is whitelisted.
function test() {
  Harness.installConfirmCallback = confirm_install;
  Harness.installsCompletedCallback = finish_test;
  Harness.setup();

  var pm = Components.classes["@mozilla.org/permissionmanager;1"]
                     .getService(Components.interfaces.nsIPermissionManager);
  pm.add(makeURI("http://example.org/"), "install", pm.ALLOW_ACTION);

  var triggers = encodeURIComponent(JSON.stringify({
    "Unsigned XPI": TESTROOT2 + "unsigned.xpi"
  }));
  gBrowser.selectedTab = gBrowser.addTab();
  gBrowser.loadURI(TESTROOT + "unsigned.xpi", makeURI(TESTROOT2 + "test.html"));
}

function confirm_install(window) {
  return false;
}

function finish_test() {
  var pm = Components.classes["@mozilla.org/permissionmanager;1"]
                     .getService(Components.interfaces.nsIPermissionManager);
  pm.remove("example.org", "install");

  gBrowser.removeCurrentTab();
  Harness.finish();
}
// ----------------------------------------------------------------------------
