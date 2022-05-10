const sections = [{ "url": "/", "en": "Dashboard" }, { "url": "/inputs", "en": "Inputs" }
    , { "url": "/channels", "en": "Channels" }, { "url": "/admin", "en": "Admin" }];

const opers = JSON.parse('%OPERS%');
const variables = JSON.parse('%VARIABLES%');
const RULE_STATEMENTS_MAX = parseInt('%RULE_STATEMENTS_MAX%');
const CHANNEL_COUNT = parseInt('%CHANNEL_COUNT%'); //parseInt hack to prevent automatic format to mess it up
const channels = JSON.parse('%channels%');  //moving data (for UI processing ) 




// https://stackoverflow.com/questions/7317273/warn-user-before-leaving-web-page-with-unsaved-changes
var formSubmitting = false;
var setFormSubmitting = function () {
    formSubmitting = true;
};

var submitChannelForm = function () {
    // set name attributes to inputs to post (not coming from server to save space)
    let inputs = document.querySelectorAll("input[id^='ctcb_'], input[id^='t_'], input[id^='chty_']"); 
    console.log("inputs.length:" + inputs.length);
    for (let i = 0; i < inputs.length; i++) {
        if (inputs[i].id != inputs[i].name) { 
        inputs[i].name = inputs[i].id;
        }
    }

    // statements from input fields 
    // clean first storage input values
    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    for (let i = 0; i < stmts_s.length; i++) {
        stmts_s[i].value = '[]';
        if (i==0)
            stmts_s[i].name = stmts_s[i].id; //send at least one fiels even if empty
    }
    // then save new values to be saved on the server
    let stmtDivs = document.querySelectorAll("div[id^='std_']");
    if (stmtDivs && stmtDivs.length > 0) {
        for (let i = 0; i < stmtDivs.length; i++) {
            const divEl = stmtDivs[i];
            const fldA = divEl.id.split("_");
            const var_val = parseInt(document.getElementById(divEl.id.replace("std", "var")).value);
            const op_val = parseInt(document.getElementById(divEl.id.replace("std", "op")).value);
            if (var_val < 0 || op_val < 0)
                continue;

            //todo decimal, test  that number valid
            const const_val = parseFloat(document.getElementById(divEl.id.replace("std", "const")).value);
            //  stmt_arr[divEl.id.replace("std_", "")] = [var_val, op_val, const_val];
            saveStoreId = "stmts_" + fldA[1] + "_" + fldA[2];
            console.log(saveStoreId + "+ " + JSON.stringify([var_val, op_val, const_val]));
            saveStoreEl = document.getElementById(saveStoreId);
            let stmt_list = [];
            if (saveStoreEl.value) {
                stmt_list = JSON.parse(saveStoreEl.value);
            }
            stmt_list.push([var_val, op_val, const_val]);
            saveStoreEl.value = JSON.stringify(stmt_list);
            saveStoreEl.name = saveStoreEl.id; // only fields with a name are posted 
        }
    }

    alert("submitChannelForm");
    formSubmitting = true;

};
var isDirty = function () { return true; }

function getVariable(variable_id) {
    for (var i = 0; i < variables.length; i++) {
        if (variables[i][0] == variable_id)
            return variables[i];
       }
    return null;
}
function addOption(el, value, text, selected = false) {
    var opt = document.createElement("option");
    opt.value = value;
    opt.selected = selected;
    opt.innerHTML = text
    // then append it to the select element
    el.appendChild(opt);
}
function populateStmtField(varFld, stmt = [-1, -1, 0]) {
    console.log("populateStmtField" + JSON.stringify(stmt));
    if (varFld.options && varFld.options.length > 0) {
        //  alert(varFld.id + " already populated");
        return; //already populated
    }
    document.getElementById(varFld.id.replace("var", "const")).style.display = "none";
    document.getElementById(varFld.id.replace("var", "op")).style.display = "none";

    addOption(varFld, -2, "remove");

    addOption(varFld, -1, "select", (stmt[0] == -1));
    for (var i = 0; i < variables.length; i++) {
      //  console.log(variables[i][0] + ", " + variables[i][1] + ", " + (stmt[0] == variables[i][0])+ ", stmt[0]:" +stmt[0] );
        addOption(varFld, variables[i][0], variables[i][1], (stmt[0] == variables[i][0]));
    }

}
function populateVariableSelects(field) {
    const selectEl = document.querySelectorAll("select[id^='var_']");
    for (let i = 0; i < selectEl.length; i++) {
        populateStmtField(selectEl[i]);
    }
}
function createElem(tagName, id = null, value = null, class_ = "", type = null) {
    const elem = document.createElement(tagName);
    if (id)
        elem.id = id;
    if (value)
        elem.value = value;
    if (class_) {
        for (const class_this of class_.split(" ")) {
            elem.classList.add(class_this);
        }

    }

    if (type)
        elem.type = type;
    return elem;
}

function addStmt(elBtn, ch_idx = -1, cond_idx = 1, stmt_idx = -1, stmt=[-1,-1,0]) {
    if (ch_idx == -1) { //from button click, no other parameters
        fldA = elBtn.id.split("_");
        ch_idx = parseInt(fldA[1]);
        cond_idx = parseInt(fldA[2]);
    }
    //get next statement index if not defined
    if (stmt_idx == -1) {  
        stmt_idx = 0;
        let div_to_search = "std_" + ch_idx + "_" + cond_idx;

        let stmtDivs = document.querySelectorAll("div[id^='" + div_to_search + "']");
        console.log("div_to_search:" + div_to_search + ", found: " + stmtDivs.length);
        for (let i = 0; i < stmtDivs.length; i++) {
            fldB = stmtDivs[i].id.split("_");
            stmt_idx = Math.max(parseInt(fldB[3]),stmt_idx);
        }

        stmt_idx++;
        console.log("New stmt_idx:" + stmt_idx);
    }

    suffix = "_" + ch_idx + "_" + cond_idx + "_" + (stmt_idx);
    console.log(suffix);

  //  lastEl = document.getElementById("std_" + ch_idx + "_" + cond_idx + "_" + stmt_idx);
    const sel_var = createElem("select", "var" + suffix, null, "fldstmt indent", null);
    sel_var.addEventListener("change", setVar);
    const sel_op = createElem("select", "op" + suffix, null, "fldstmt", null);
    sel_op.on_change = 'setOper(this)';
    const inp_const = createElem("input", "const" + suffix, 0, "fldtiny fldstmt inpnum", "text");
    const div_std = createElem("div", "std" + suffix, null, "divstament", "text");
    div_std.appendChild(sel_var);
    div_std.appendChild(sel_op);
    div_std.appendChild(inp_const);

    elBtn.parentNode.insertBefore(div_std, elBtn);
    populateStmtField(document.getElementById("var" + suffix), stmt);
}




function populateTemplateSel(selEl,template_id=-1) {
    if (selEl.options && selEl.options.length > 0) {
        return; //already populated
    }
  
    addOption(selEl, -1, "select", false);
    $.getJSON('/data/template-list.json', function (data) {
        $.each(data, function (i, row) {
            addOption(selEl, row["id"], row["name"], (template_id==row["id"]));     
        });
    });

}

function templateChanged(selEl) {
    const fldA = selEl.id.split("_");
    channel_idx = fldA[1];
    console.log(selEl.value);
    template_idx = selEl.value;
    url = '/data/templates?id=' + template_idx;
    console.log(url);
    $.getJSON(url, function (data) {
        alert(JSON.stringify(data));
        deleteStmtsUI(channel_idx);

        $.each(data.conditions, function (cond_idx,  rule) {
            alert(" cond_idx:" + cond_idx + "  rule json:" + JSON.stringify(rule)); 
                document.getElementById("ctcb_" + channel_idx + "_" + cond_idx).checked = rule["on"];
            
            $.each(rule.statements, function (j, stmt) {

                elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
                console.log("stmt.values:" + JSON.stringify(stmt.values));
                addStmt(elBtn, channel_idx, cond_idx, j, stmt.values);
                var_this = get_var_by_id(stmt.values[0]); 
                populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmt.values);
            });
        });

        // nyt ...
       // elBtn = document.getElementById("addstmt_" + ch_idx + "_" + cond_idx);
       // addStmt(elBtn,ch_idx, cond_idx, j, stmts[j]);
 
    });
}
function deleteStmtsUI(ch_idx) {

    selector_str = "div[id^='std_" + ch_idx + "']";
    document.querySelectorAll(selector_str).forEach(e => e.remove());
}
function setRuleMode(ch_idx,rule_mode, reset,template_id) {
   // alert("ch_idx:" + ch_idx + "   reset:" + reset);
    $('#rd_' + ch_idx + ' select').attr('disabled', (rule_mode != 0));
    $('#rd_' + ch_idx + ' input').attr('disabled', (rule_mode != 0));

    
    $('#rt_' + ch_idx + ' select').attr('disabled', (rule_mode != 1));
    $('#rt_' + ch_idx + ' input').attr('disabled', (rule_mode != 1));
    if (rule_mode == 1) {
        templateSelEl = document.getElementById("rts_" + ch_idx);
        populateTemplateSel(templateSelEl,template_id);
    }
}

function populateOper(el, var_this, stmt = [-1, -1, 0]) {
    console.log("populateOper:" + el.id);
    console.log(JSON.stringify(var_this));
    console.log(JSON.stringify(stmt));
    //set constant, TODO: handle exp
    document.getElementById(el.id.replace("op", "const")).value = stmt[2];

    if (el.options.length == 0)
        addOption(el, -1, "select", (stmt[1] == -1));
    if (el.options)
        while (el.options.length > 1) {
            el.remove(1);
        }
    if (var_this) {
        for (let i = 0; i < opers.length; i++) {
            if (var_this[2] >= 50 && !opers[i][5]) //2-type
                continue;
            if (var_this[2] < 50 && opers[i][5])
                continue;
            const_id = el.id.replace("op", "const");
          //  console.log(const_id);
            el.style.display = "block";
            document.getElementById(el.id.replace("op", "const")).style.display = (opers[i][5]) ? "none" : "block";
            addOption(el, opers[i][0], opers[i][1], (opers[i][0] == stmt[1]));
        }
    }
    else {
        ;
    }

}
function get_var_by_id(id) {
    for (var i = 0; i < variables.length; i++) {
        if (variables[i][0] == id) {
            return variables[i];
        }
    };
}
// set variable in dropdown select
function setVar(evt) {
    const el = evt.target;
    const fldA = el.id.split("_");
    ch_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);
    stmt_idx = parseInt(fldA[3]);
    if (el.value > -1) {
        /*
        for (var i = 0; i < variables.length; i++) {
            if (variables[i][0] == el.value) {
                var_this = variables[i];
                break;
            }
        } */
        // let var_this = variables[el.value];
        var_this = get_var_by_id(el.value);
        populateOper(document.getElementById(el.id.replace("var", "op")), var_this);
    }
    else if (el.value == -2) {
        // if (!document.getElementById("var_" + ch_idx + "_" + cond_idx + "_" + stmt_idx + 2) && stmt_idx>0) { //is last
        if (confirm("Confirm")) {
            //viimeinen olisi hyvä jättää, jos poistaa välistä niin numerot frakmentoituu
            var elem = document.getElementById(el.id.replace("var", "std"));
            elem.parentNode.removeChild(elem);
        }
        // }
    }

}
function setOper(el) {

}


function setEnergyMeterFields(val) { //
    idx = parseInt(val);

    var emhd = document.querySelector('#emhd');
    var empd = document.querySelector('#empd');
    var emhd = document.querySelector('#emhd');
    var emidd = document.querySelector('#emidd');
    var baseld = document.querySelector('#baseld');

    if ([1, 2, 3].indexOf(idx) > -1) {
        emhd.style.display = "block";
        empd.style.display = "block";
    } else {
        emhd.style.display = "none";
        empd.style.display = "none";
    }
    if ([2, 3].indexOf(idx) > -1)
        baseld.style.display = "block";
    else
        baseld.style.display = "none";

    if ([3].indexOf(idx) > -1) {
        empd.style.display = "block";
        emidd.style.display = "block";
    } else {
        empd.style.display = "none";
        emidd.style.display = "none";
    }

}
function initChannelForm() {
    console.log("initChannelForm");
    var chtype;
    for (var ch_idx = 0; ch_idx < CHANNEL_COUNT; ch_idx++) {
        //ch_t_%d_1
        console.log("ch_idx:" + ch_idx);
        //TODO: fix if more types coming
        chtype = document.getElementById('chty_' + ch_idx).value;
        setChannelFieldsByType(ch_idx, chtype);

        rule_mode = channels[ch_idx]["cm"];
        template_id = channels[ch_idx]["tid"];
        console.log("rule_mode: " + rule_mode + ", template_id:" + template_id);

        setRuleMode(ch_idx, rule_mode, false,template_id);
        if (rule_mode == 1) {      
            templateSelEl = document.getElementById("rts_" + ch_idx);
            templateSelEl.value = template_id;
            console.log("templateSelEl.value:" + templateSelEl.value);
        }

    }

    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    console.log("stmts_s.length:" + stmts_s.length);
    for (let i = 0; i < stmts_s.length; i++) {
        if (stmts_s[i].value) {
            fldA = stmts_s[i].id.split("_");
            ch_idx = parseInt(fldA[1]);
            cond_idx = parseInt(fldA[2]);
            console.log(stmts_s[i].value);
            stmts = JSON.parse(stmts_s[i].value);
            if (stmts && stmts.length > 0) {
                console.log(stmts_s[i].id + ": " + JSON.stringify(stmts));
                for (let j = 0; j < stmts.length; j++) {
                    elBtn = document.getElementById("addstmt_" + ch_idx + "_" + cond_idx);
                    addStmt(elBtn,ch_idx, cond_idx, j, stmts[j]);
                    var_this = get_var_by_id(stmts[j][0]); //vika indeksi oli 1
                    populateOper(document.getElementById("op_" + ch_idx + "_" + cond_idx + "_" + j), var_this, stmts[j]);
                }
            }
        }
    }
}

function setChannelFieldsByType(ch, chtype) {
    for (var t = 0; t < RULE_STATEMENTS_MAX; t++) {
        divid = 'td_' + ch + "_" + t;
     //   var targetdiv = document.querySelector('#' + divid);
        var cbdiv = document.querySelector('#ctcbd_' + ch + "_" + t);
      //  var isTarget = (1 & chtype);
        var uptimediv = document.querySelector('#d_uptimem_' + ch);

        if (chtype == 0)
            uptimediv.style.display = "none";
        else
            uptimediv.style.display = "block";
/*
        if (isTarget) {
            targetdiv.style.display = "block";
            cbdiv.style.display = "none";
        } else {
            targetdiv.style.display = "none";
            cbdiv.style.display = "block";
        } */
    } 
}
function initUrlBar(url) {
    var headdiv = document.getElementById("headdiv");

    var h1 = document.createElement('h1');
    h1.innerHTML = "Arska Node";
    headdiv.appendChild(h1);

    sections.forEach((sect, idx) => {
        if (url == sect.url) {
            var span = document.createElement('span');
            var b = document.createElement('b');
            b.innerHTML = sect.en;
            span.appendChild(b);
            headdiv.appendChild(span);
        }
        else {
            var a = document.createElement('a');
            var link = document.createTextNode(sect.en);
            a.appendChild(link);
            a.title = sect.en;
            a.href = sect.url;
            headdiv.appendChild(a);
        }

        if (idx < sections.length - 1) {
            var sepa = document.createTextNode(" | ");
            headdiv.appendChild(sepa);
        }
    });
    // <a href="/">Dashboard</a> | <span> <b>Admin</b> </span>

}

//
function initForm(url) {
    initUrlBar(url);
    if (url == '/admin') {
        initWifiForm();
    }
    else if (url == '/channels') {
        initChannelForm();
    }
    var footerdiv = document.getElementById("footerdiv");
    if (footerdiv) {
        footerdiv.innerHTML = "<a href='http://netgalleria.fi/rd/?arska-wiki' target='arskaw'>Arska Wiki</a> | <a href='http://netgalleria.fi/rd/?arska-states' target='arskaw'>States</a> | <a href='http://netgalleria.fi/rd/?arska-rulesets'  target='arskaw'>Rulesets</a>";
    }
}

function compare_wifis(a, b) {
    return ((a.rssi > b.rssi) ? -1 : 1);
}

function initWifiForm() {
    var wifisp = JSON.parse(wifis);
    wifisp.sort(compare_wifis);
    var wifi_sel = document.getElementById("wifi_ssid");
    wifisp.forEach(wifi => {
        if (wifi.id) {
            var opt = document.createElement("option");
            opt.value = wifi.id;
            // if (wifi.id == wifi_ssid) //from constants via template processing
            //     opt.selected = true;
            opt.innerHTML = wifi.id + ' (' + wifi.rssi + ')';
            wifi_sel.appendChild(opt);
        }
    })
}

function setChannelFields(obj) {
    if (obj == null)
        return;
    const fldA = obj.id.split("_");
    ch = parseInt(fldA[1]);
    var chtype = obj.value;
    setChannelFieldsByType(ch, chtype);
}

//beforeAdminSubmit
function checkAdminForm() {
    document.querySelector('#ts').value = Date.now() / 1000;
    var http_password = document.querySelector('#http_password').value;
    var http_password2 = document.querySelector('#http_password2').value;
    if (http_password && http_password.length < 5) {
        alert('Password too short, minimum 5 characters.');
        return false;
    }
    if (http_password != http_password2) {
        alert('Passwords do not match!');
        return false;
    }
    return true;
}

function clearText(elem) {
    elem.value = '';
}
// Ruleset processing
function processRulesetImport(evt) {
    if (!evt.target.id.startsWith("rules_"))
        return
    const fldA = evt.target.id.split("_");
    channel_idx = parseInt(fldA[1]);

    let data = evt.clipboardData.getData('text/plain');
    try {
        obj = JSON.parse(data);
        evt.target.value = 'x';
        document.getElementById(evt.target.id).value = '';
        if ((!'rulesetId' in obj) || !('rules' in obj)) {
            alert("Invalid ruleset data.");
            return;
        }
        let import_it = confirm("Do you want to import rule set over existing rules?");

    } catch (e) {
        alert('Invalid ruleset (' + e + ')'); // error in the above string (in this case, yes)!
        evt.target.value = '';
        return false;
    }

    for (let i = 0; i < CHANNEL_COUNT; i++) {
        let rule_states_e = document.getElementById("st_" + channel_idx + "_" + i);
        let rule_onoff_cb = document.getElementById("ctcb_" + channel_idx + "_" + i);
        let rule_target_e = document.getElementById("t_" + channel_idx + "_" + i);

        if (obj["rules"].length >= i + 1) {
            rule_states_e.value = JSON.stringify(obj["rules"][i]["states"]).replace("[", "").replace("]", "");
            rule_onoff_cb.checked = obj["rules"][i]["on"];
            if (obj["rules"][i]["target"]) {
                rule_target_e.value = obj["rules"][i]["target"];
            }
            else {
                rule_target_e.value = 0;
            }
        }
        else {
            rule_states_e.value = '';
            rule_onoff_cb.checked = false;
            rule_target_e.value = 0;

        }
    }
}
let rs1 = document.getElementById('rs1')
document.addEventListener('paste', e => {
    processRulesetImport(e);
})

