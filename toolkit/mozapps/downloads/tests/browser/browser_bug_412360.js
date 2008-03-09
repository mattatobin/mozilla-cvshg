/* ***** BEGIN LICENSE BLOCK *****
 * Version: MPL 1.1/GPL 2.0/LGPL 2.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is Download Manager UI Test Code.
 *
 * The Initial Developer of the Original Code is
 * Edward Lee <edward.lee@engineering.uiuc.edu>.
 * Portions created by the Initial Developer are Copyright (C) 2008
 * the Initial Developer. All Rights Reserved.
 *
 * Contributor(s):
 *
 * Alternatively, the contents of this file may be used under the terms of
 * either the GNU General Public License Version 2 or later (the "GPL"), or
 * the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
 * in which case the provisions of the GPL or the LGPL are applicable instead
 * of those above. If you wish to allow use of your version of this file only
 * under the terms of either the GPL or the LGPL, and not to allow others to
 * use your version of this file under the terms of the MPL, indicate your
 * decision by deleting the provisions above and replace them with the notice
 * and other provisions required by the GPL or the LGPL. If you do not delete
 * the provisions above, a recipient may use your version of this file under
 * the terms of any one of the MPL, the GPL or the LGPL.
 *
 * ***** END LICENSE BLOCK ***** */

Components.utils.import("resource://gre/modules/XPCOMUtils.jsm");

let didFail = false;

let promptService = {
  QueryInterface: XPCOMUtils.generateQI([Ci.nsIPromptService]),
  alert: function() {
    ok(didFail, "javascript: uri failed and showed a message");
    finish();
  }
};

Components.manager.QueryInterface(Ci.nsIComponentRegistrar).
registerFactory(Components.ID("{6cc9c9fe-bc0b-432b-a410-253ef8bcc699}"),
  "PromptService", "@mozilla.org/embedcomp/prompt-service;1",
  {
    createInstance: function(aOuter, aIid) {
      if (aOuter != null)
        throw Components.results.NS_ERROR_NO_AGGREGATION;
      return promptService.QueryInterface(aIid);
    }
  });

function test()
{
  let dm = Cc["@mozilla.org/download-manager;1"].
           getService(Ci.nsIDownloadManager);
  let db = dm.DBConnection;

  // Empty any old downloads
  db.executeSimpleSQL("DELETE FROM moz_downloads");

  let stmt = db.createStatement(
    "INSERT INTO moz_downloads (source, target, state) " +
    "VALUES (?1, ?2, ?3)");

  // Saving javascript URIs doesn't work
  stmt.bindStringParameter(0, "javascript:5");

  // Download to a temp local file
  let file = Cc["@mozilla.org/file/directory_service;1"].
             getService(Ci.nsIProperties).get("TmpD", Ci.nsIFile);
  file.append("javascriptURI");
  file.createUnique(Ci.nsIFile.NORMAL_FILE_TYPE, 0666);
  stmt.bindStringParameter(1, Cc["@mozilla.org/network/io-service;1"].
    getService(Ci.nsIIOService).newFileURI(file).spec);

  // Start it as canceled
  stmt.bindInt32Parameter(2, dm.DOWNLOAD_CANCELED);

  // Add it!
  stmt.execute();
  stmt.finalize();

  let listener = {
    onDownloadStateChange: function(aState, aDownload)
    {
      switch (aDownload.state) {
        case dm.DOWNLOAD_FAILED:
          // Remember that we failed for the prompt service
          didFail = true;

          ok(true, "javascript: uri failed instead of getting stuck");
          dm.removeListener(listener);
          break;
      }
    }
  };
  dm.addListener(listener);

  // Close the UI if necessary
  let wm = Cc["@mozilla.org/appshell/window-mediator;1"].
           getService(Ci.nsIWindowMediator);
  let win = wm.getMostRecentWindow("Download:Manager");
  if (win) win.close();

  // Start the test when the download manager window loads
  let ww = Cc["@mozilla.org/embedcomp/window-watcher;1"].
           getService(Ci.nsIWindowWatcher);
  ww.registerNotification({
    observe: function(aSubject, aTopic, aData) {
      ww.unregisterNotification(this);
      aSubject.QueryInterface(Ci.nsIDOMEventTarget).
      addEventListener("DOMContentLoaded", doTest, false);
    }
  });

  // Let the Startup method of the download manager UI finish before we test
  let doTest = function() setTimeout(function() {
    win = wm.getMostRecentWindow("Download:Manager");

    // Try again if selectedIndex is -1
    if (win.document.getElementById("downloadView").selectedIndex)
      return doTest();

    // Send the enter key to Download Manager to retry the download
    EventUtils.synthesizeKey("VK_ENTER", {}, win);
  }, 0);
 
  // Show the Download Manager UI
  Cc["@mozilla.org/download-manager-ui;1"].
  getService(Ci.nsIDownloadManagerUI).show();

  waitForExplicitFinish();
}
