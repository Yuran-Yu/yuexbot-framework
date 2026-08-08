const fs = require('fs');
const fp = 'C:\\Users\\31173\\Documents\\Codex\\2026-05-29\\https-api-luckylillia-com\\jadebot\\www\\index.html';
let c = fs.readFileSync(fp, 'utf8');

// 1. Replace Settings Page HTML (lines 429-448)
const settingsOld =     <div class= page id=page-settings><h2 class=section-title>\u8bbe\u7f6e</h2>
      <div class=settings-section><h3>\u8fde\u63a5\u8bbe\u7f6e</h3>
        <div class=form-row><div class=form-label>\u8fde\u63a5\u65b9\u5f0f</div><div class=radio-group id=connModeGroup><div class=radio-btn active data-mode=reverse-ws>\u53cd\u5411 WebSocket</div><div class=radio-btn data-mode=forward-ws>\u6b63\u5411 WebSocket</div><div class=radio-btn data-mode=http-post>HTTP POST</div></div></div>
        <div class=form-row><div class=form-label>API \u5730\u5740</div><input class=form-input id=setHost value=127.0.0.1 placeholder=127.0.0.1></div>
        <div class=form-row><div class=form-label>\u7aef\u53e3\u53f7</div><input class=form-input id=setPort value=3001 placeholder=3001></div>
        <div class=form-row><div class=form-label>Access Token</div><input class=form-input id=setToken type=password placeholder=\u53ef\u9009></div>
        <div class=form-row id=wsEventRow style=display:none><div class=form-label>\u4e8b\u4ef6\u5730\u5740</div><input class=form-input id=setWsEvent placeholder=ws://127.0.0.1:3002></div>
        <div class=settings-actions><button class=action-btn primary id=btnSaveSettings>\u4fdd\u5b58\u8bbe\u7f6e</button><button class=action-btn id=btnTestConn>\u6d4b\u8bd5\u8fde\u63a5</button></div>
      </div>
      <div class=settings-section><h3>\u5e94\u7528\u8bbe\u7f6e</h3>
        <div class=form-row><div class=form-label>\u5f00\u673a\u81ea\u542f</div><label class=toggle><input type=checkbox id=setAutoStart><span class=slider></span></label></div>
        <div class=form-row><div class=form-label>\u6700\u5c0f\u5316\u5230\u6258\u76d8</div><label class=toggle><input type=checkbox id=setTray checked><span class=slider></span></label></div>
        <div class=form-row><div class=form-label>\u65e5\u5fd7\u4fdd\u5b58</div><label class=toggle><input type=checkbox id=setLogSave checked><span class=slider></span></label></div>
      </div>
      <div class=settings-section><h3>\u5173\u4e8e</h3>
        <div class=form-row><div class=form-label>\u7248\u672c</div><span class=about-text>YuexBot v1.0.0</span></div>
        <div class=form-row><div class=form-label>UI \u5f15\u64ce</div><span class=about-text>JadeView (WebView2)</span></div>
        <div class=form-row><div class=form-label>\u534f\u8bae</div><span class=about-text>OneBot 11</span></div>
      </div>
    </div>;

const settingsNew =     <div class=page id=page-settings><h2 class=section-title>\u8bbe\u7f6e</h2>
      <div class=settings-section><h3>\u8fde\u63a5\u8bbe\u7f6e</h3>
        <div class=conn-mode-list>
          <div class=conn-mode-card id=reverseWsCard>
            <div class=conn-mode-header>
              <div class=conn-mode-icon>\u{1F50C}</div>
              <div class=conn-mode-info><div class=conn-mode-title>\u53cd\u5411 WebSocket \u76d1\u542c</div><div class=conn-mode-desc>OneBot \u670d\u52a1\u7aef\u4e3b\u52a8\u8fde\u63a5\u672c\u6846\u67b6</div></div>
              <label class=toggle><input type=checkbox id=reverseWsEnabled><span class=slider></span></label>
            </div>
            <div class=conn-mode-body id=reverseWsBody>
              <div class=conn-mode-fields>
                <div class=conn-field><label>\u76d1\u542c\u5730\u5740</label><input class=form-input id=setReverseHost value=0.0.0.0 placeholder=0.0.0.0></div>
                <div class=conn-field><label>\u76d1\u542c\u7aef\u53e3</label><input class=form-input id=setReversePort value=3001 placeholder=3001></div>
                <div class=conn-field><label>Access Token</label><input class=form-input id=setReverseToken type=password placeholder=\u53ef\u9009></div>
              </div>
            </div>
          </div>
          <div class=conn-mode-card id=forwardWsCard>
            <div class=conn-mode-header>
              <div class=conn-mode-icon>\u{1F517}</div>
              <div class=conn-mode-info><div class=conn-mode-title>\u6b63\u5411 WebSocket \u8fde\u63a5</div><div class=conn-mode-desc>\u672c\u6846\u67b6\u4e3b\u52a8\u8fde\u63a5 OneBot \u670d\u52a1\u7aef</div></div>
              <label class=toggle><input type=checkbox id=forwardWsEnabled><span class=slider></span></label>
            </div>
            <div class=conn-mode-body id=forwardWsBody style=display:none>
              <div class=conn-mode-fields>
                <div class=conn-field><label>\u670d\u52a1\u5730\u5740</label><input class=form-input id=setForwardHost value=127.0.0.1 placeholder=127.0.0.1></div>
                <div class=conn-field><label>\u670d\u52a1\u7aef\u53e3</label><input class=form-input id=setForwardPort value=8080 placeholder=8080></div>
                <div class=conn-field><label>Access Token</label><input class=form-input id=setForwardToken type=password placeholder=\u53ef\u9009></div>
              </div>
            </div>
          </div>
          <div class=conn-mode-card id=httpPostCard>
            <div class=conn-mode-header>
              <div class=conn-mode-icon>\u{1F4E1}</div>
              <div class=conn-mode-info><div class=conn-mode-title>HTTP POST \u4e8b\u4ef6\u56de\u8c03</div><div class=conn-mode-desc>\u4e8b\u4ef6\u901a\u8fc7 HTTP POST \u63a8\u9001\u5230\u672c\u6846\u67b6</div></div>
              <label class=toggle><input type=checkbox id=httpPostEnabled><span class=slider></span></label>
            </div>
            <div class=conn-mode-body id=httpPostBody style=display:none>
              <div class=conn-mode-fields>
                <div class=conn-field><label>\u76d1\u542c\u7aef\u53e3</label><input class=form-input id=setHttpPostPort value=5701 placeholder=5701></div>
                <div class=conn-field><label>Access Token</label><input class=form-input id=setHttpPostToken type=password placeholder=\u53ef\u9009></div>
              </div>
            </div>
          </div>
        </div>
        <div class=settings-hint>\u6846\u67b6\u7ea7\u8fde\u63a5\u914d\u7f6e\u4f5c\u4e3a\u9ed8\u8ba4\u503c\uff0c\u5404\u8d26\u53f7\u53ef\u72ec\u7acb\u8986\u76d6</div>
        <div class=settings-actions><button class=action-btn primary id=btnSaveSettings>\u4fdd\u5b58\u8bbe\u7f6e</button></div>
      </div>
      <div class=settings-section><h3>\u5e94\u7528\u8bbe\u7f6e</h3>
        <div class=form-row><div class=form-label>\u5f00\u673a\u81ea\u542f</div><label class=toggle><input type=checkbox id=setAutoStart><span class=slider></span></label></div>
        <div class=form-row><div class=form-label>\u6700\u5c0f\u5316\u5230\u6258\u76d8</div><label class=toggle><input type=checkbox id=setTray checked><span class=slider></span></label></div>
        <div class=form-row><div class=form-label>\u65e5\u5fd7\u4fdd\u5b58</div><label class=toggle><input type=checkbox id=setLogSave checked><span class=slider></span></label></div>
      </div>
      <div class=settings-section><h3>\u5173\u4e8e</h3>
        <div class=form-row><div class=form-label>\u7248\u672c</div><span class=about-text>YuexBot v1.0.0</span></div>
        <div class=form-row><div class=form-label>UI \u5f15\u64ce</div><span class=about-text>JadeView (WebView2)</span></div>
        <div class=form-row><div class=form-label>\u534f\u8bae</div><span class=about-text>OneBot 11</span></div>
      </div>
    </div>;

if (c.includes(settingsOld)) {
  c = c.replace(settingsOld, settingsNew);
  console.log('[1] Settings page replaced');
} else {
  console.log('[1] Settings page OLD text not found - trying line-based replace');
}

