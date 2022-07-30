/*
Copyright, Netgalleria Oy, Olli Rinne 2021-2022
*/
const sections = [{ "url": "/", "en": "Dashboard", "wiki": "Dashboard" }, { "url": "/inputs", "en": "Services", "wiki": "Edit-Services" }
    , { "url": "/channels", "en": "Channels", "wiki": "Edit-Channels" }, { "url": "/admin", "en": "Admin", "wiki": "Edit-Admin" }];

const opers = JSON.parse('%OPERS%');
const variables = JSON.parse('%VARIABLES%');
const CHANNEL_COUNT = parseInt('%CHANNEL_COUNT%'); //parseInt hack to prevent automatic format to mess it up
const CHANNEL_CONDITIONS_MAX = parseInt('%CHANNEL_CONDITIONS_MAX%');
const RULE_STATEMENTS_MAX = parseInt('%RULE_STATEMENTS_MAX%');
const channels = JSON.parse('%channels%');  //moving data (for UI processing ) 
const channel_type_strings = JSON.parse('%channel_type_strings%');
const lang = '%lang%';
const using_default_password = ('%using_default_password%' === 'true');
const backup_wifi_config_mode = ('%backup_wifi_config_mode%' === 'true');
const DEBUG_MODE = ('%DEBUG_MODE%' === 'true');
const VERSION = '%VERSION%';
const HWID = '%HWID%';
const VERSION_SHORT = '%VERSION_SHORT%';
const version_fs = '%version_fs%';

const VARIABLE_LONG_UNKNOWN = -2147483648


let variable_list = {}; // populate later

// https://stackoverflow.com/questions/7317273/warn-user-before-leaving-web-page-with-unsaved-changes
var formSubmitting = false;
var setFormSubmitting = function () {
    formSubmitting = true;
};

//disable/enable UI elements depending on varible mode (source/replica)
function setVariableMode(variable_mode) {
    document.getElementById("entsoe_api_key").disabled = (variable_mode != 0);
    document.getElementById("entsoe_area_code").disabled = (variable_mode != 0);
    document.getElementById("forecast_loc").disabled = (variable_mode != 0);
    document.getElementById("variable_server").disabled = (variable_mode != 1);
}


function statusCBClicked(elCb) {
    //if (elCb.checked) {
    setTimeout(function () { updateStatus(elCb.checked); }, 1000);
    //}
    document.getElementById("variables").style.display = document.getElementById("statusauto").checked ? "block" : "none";
}

function link_to_wiki(article_name) {
    return '<a class="helpUrl" target="wiki" href="https://github.com/Netgalleria/arska-node/wiki/' + article_name + '">ℹ</a>';
}

function get_date_string_from_ts(ts) {
    tmpDate = new Date(ts * 1000);
    return tmpDate.getFullYear() + '-' + ('0' + (tmpDate.getMonth() + 1)).slice(-2) + '-' + ('0' + tmpDate.getDate()).slice(-2) + ' ' + tmpDate.toLocaleTimeString();
}
var prices = [];
var prices_first_ts = 0;
var prices_last_ts = 0;
var prices_resolution_min = 60;
var prices_expires = 0;


function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function get_price_data() {
    now_ts = Date.now() / 1000;
    if (prices_expires > now_ts)
        return;
    console.log("get_price_data, wait first");
    //await sleep(5000);
    console.log("get_price_data, now requesting");

    $.ajax({
        url: '/data/price-data.json',
        dataType: 'json',
        async: false,
        success: function (data) {
            console.log('/data/price-data.json');
            //   $.each(data.ch, function (i, ch_cur) {
            //    }
            prices = data.prices;
            prices_first_ts = data.record_start;
            prices_resolution_min = data.resolution_m;
            prices_last_ts = prices_first_ts + (prices.length - 1) * (prices_resolution_min * 60);
            prices_expires = data.expires;
            setTimeout(function () { get_price_data(); }, 1800000);
        },
        fail: function () {
            console.log("Cannot get prices");
            setTimeout(function () { get_price_data(); }, 10000);
        }
    });
    //  });
    // next update (if expired)
    // setTimeout(function () { get_price_data(); }, 1800000);
}

function get_price_for_block(start_ts, end_ts = 0) {
    if (prices.length == 0) { //prices (not yet) populated
        console.log("get_price_for_block, prices not populated              ");
        return -VARIABLE_LONG_UNKNOWN;
    }
    if (end_ts == 0)
        end_ts = start_ts;
    if (start_ts < prices_first_ts || end_ts > prices_last_ts) {
        //   console.log("no match", start_ts, prices_first_ts, end_ts, prices_last_ts);
        return -VARIABLE_LONG_UNKNOWN;
    }
    var price_count = 0;
    var price_sum = 0;

    for (cur_ts = start_ts; cur_ts <= end_ts; cur_ts += (prices_resolution_min * 60)) {
        price_idx = (cur_ts - prices_first_ts) / (prices_resolution_min * 60);
        //   console.log("price",cur_ts,price_idx,prices[price_idx])
        price_sum += prices[price_idx];
        price_count++;
    }

    var block_price_avg = (price_sum / price_count) / 1000;
    //  console.log("get_price_for_block result:", start_ts,end_ts,block_price_avg,price_sum,price_count);

    return block_price_avg;
}

function get_time_string_from_ts(ts, show_day_diff = false) {
    // console.log("get_time_string_from_ts",ts)
    tmpDate = new Date(ts * 1000);
    tmpStr = tmpDate.toLocaleTimeString();
    if (show_day_diff) {
        tz_offset_minutes = tmpDate.getTimezoneOffset();
        //  console.log("tz_offset_minutes",tz_offset_minutes);
        now_ts_loc = (Date.now() / 1000) - tz_offset_minutes * 60;
        ts_loc = ts - tz_offset_minutes * 60;
        now_day = parseInt(now_ts_loc / 86400);
        ts_day = parseInt(ts_loc / 86400);

        day_diff = ts_day - now_day;
        if (day_diff != 0)
            tmpStr += " (" + ((day_diff > 0) ? "+" : "") + (day_diff) + ")";

    }
    return tmpStr;
}


// update variables and channels statuses to channels form
function updateStatus(show_variables = true) {

    update_schedule_select_periodical(); //TODO:once after hour/period change should be enough

    $.getJSON('/status', function (data) {
        msgdiv = document.getElementById("msgdiv");
        keyfd = document.getElementById("keyfd");
        if (msgdiv) {
            if (data.last_msg_ts > getCookie("msg_read")) {
                //msgDate = new Date(data.last_msg_ts * 1000);
                //msgDateStr = msgDate.getFullYear() + '-' + ('0' + (msgDate.getMonth() + 1)).slice(-2) + '-' + ('0' + msgDate.getDate()).slice(-2);
                msgDateStr = get_time_string_from_ts(data.last_msg_ts, true);
                msgdiv.innerHTML = '<span class="msg' + data.last_msg_type + '">' + msgDateStr + ' ' + data.last_msg_msg + '<button class="smallbtn" onclick="set_msg_read(this)" id="btn_msgread">✔</button></span>';
            }
        }
        if (keyfd) {
            selling = data.variables["102"];
            price = data.variables["0"];
            sensor_text = '';

            emDate = new Date(data.energym_read_last * 1000);

            for (i = 0; i < 3; i++) {
                if (data.variables[(i + 201).toString()] != "null") {
                    if (sensor_text)
                        sensor_text += ", ";
                    sensor_text += 'Sensor ' + (i + 1) + ': <span  class="big">' + data.variables[(i + 201).toString()] + ' &deg;C</span>';
                }
            }
            if (sensor_text)
                sensor_text = "<br>" + sensor_text;

            selling_text = (selling > 0) ? "Selling ⬆ " : "Buying ⬇ ";
            keyfd.innerHTML = selling_text + '<span class="big">' + Math.abs(selling) + ' W</span> (period average ' + emDate.toLocaleTimeString() + '), Price: <span class="big">' + price + ' ¢/kWh </span>' + sensor_text;
        }
        if (show_variables) {
            document.getElementById("variables").style.display = document.getElementById("statusauto").checked ? "block" : "none";

            $("#tblVariables_tb").empty();
            $.each(data.variables, function (i, variable) {
                var_this = getVariable(i);
                if (var_this[0] in variable_list)
                    variable_desc = variable_list[var_this[0]]["en"]; //TODO: multilang
                else
                    variable_desc = "";

                if (var_this[2] == 50 || var_this[2] == 51) {
                    newRow = '<tr><td>' + var_this[1] + '</td><td>' + variable.replace('"', '').replace('"', '') + '</td><td>' + variable_desc + ' (logical)</td></tr>';
                    //newRow = '<tr><td>X' + var_this[1] + '</td><td>' + variable.replace('"', '').replace('"', '') + '</td><td>' + variable_desc + ' (numeric)</td></tr>';
                }
                else {
                    newRow = '<tr><td>' + var_this[1] + '</td><td>' + variable.replace('"', '').replace('"', '') + '</td><td>' + variable_desc + ' (numeric)</td></tr>';
                }
                $(newRow).appendTo($("#tblVariables_tb"));
            });
            $('<tr><td>updated</td><td>' + data.localtime.substring(11) + '</td></tr></table>').appendTo($("#tblVariables_tb"));
        }
        $.each(data.ch, function (i, ch) {
            show_channel_status(i, ch)
        });
    });
    //  if (document.getElementById('statusauto').checked) {
    setTimeout(function () { updateStatus(show_variables); }, 30000);
    //  }
    //  get_price_data(); // get prices, if expired or not fetched before
}



function show_channel_status(channel_idx, ch) {
    now_ts = Date.now() / 1000;
    // console.log(channel_idx,ch);
    status_el = document.getElementById("status_" + channel_idx); //.href = is_up ? "#green" : "#red";
    href = ch.is_up ? "#green" : "#red";
    if (status_el)
        status_el.setAttributeNS('http://www.w3.org/1999/xlink', 'href', href);

    info_text = "";

    if (ch.is_up) {
        if (ch.force_up)
            info_text += "Up based on manual schedule: " + get_time_string_from_ts(ch.force_up_from, true) + " --> " + get_time_string_from_ts(ch.force_up_until, true) + ". ";
        else if (ch.active_condition > -1)
            info_text += "Up based on <a class='chlink'  href='/channels#c" + channel_idx + "r" + ch.active_condition + "'>rule " + (ch.active_condition + 1) + "</a>. ";
    }
    else {
        if ((ch.active_condition > -1))
            info_text += "Down based on <a class='chlink' href='/channels#c" + channel_idx + "r" + ch.active_condition + "'>rule " + (ch.active_condition + 1) + "</a>. ";
        if ((ch.active_condition == -1))
            info_text += "Down, no matching rules. ";
    }

    if (ch.force_up_from > now_ts-100) { //leave some margin
        if (info_text)
            info_text += "<br>";
        info_text += "Scheduled: " + get_time_string_from_ts(ch.force_up_from, true) + " --> " + get_time_string_from_ts(ch.force_up_until, true);
    }

    chdiv_info = document.getElementById("chdinfo_" + channel_idx);
    // chdiv_info.insertAdjacentHTML('beforeend', "<br><span>" + info_text + "</span>");
    if (chdiv_info)
        chdiv_info.innerHTML = info_text;
   
}
// before submitting input admin form
var submitInputsForm = function (e) {
    if (!confirm("Save and restart."))
        return false;
    formSubmitting = true;
    return true;
}

// Before submitting channels config form
var submitChannelForm = function (e) {
    //e.preventDefault();
    if (!confirm("Save channel settings?"))
        return false;

    // set name attributes to inputs to post (not coming from server to save space)
    //TODO: in new UI model we could have names set by js element creation 
    let inputs = document.querySelectorAll("input[id^='t_'], input[id^='chty_']");
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
        if (i == 0)
            stmts_s[i].name = stmts_s[i].id; //send at least one field even if empty
    }

    // then save new values to be saved on the server
    let stmtDivs = document.querySelectorAll("div[id^='std_']");

    if (stmtDivs && stmtDivs.length > 0) {
        for (let i = 0; i < stmtDivs.length; i++) {
            // console.log("stmp element ", i);
            const divEl = stmtDivs[i];
            const fldA = divEl.id.split("_");
            suffix = divEl.id.replace("std_", "_");
            const var_val = parseInt(document.getElementById("var" + suffix).value);
            const op_val = parseInt(document.getElementById("op" + suffix).value);

            if (var_val < 0 || op_val < 0)
                continue; // no complete rule

            //todo decimal, test  that number valid
            const const_val = parseFloat(document.getElementById("const" + suffix).value);
            saveStoreId = "stmts_" + fldA[1] + "_" + fldA[2];

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

    if (window.location.search.includes("devel=1")) {
        let stmts_s = document.querySelectorAll("input[id^='stmts_']");
        let prev_channel_idx = 0;
        channel_rules = [];
        for (let i = 0; i < stmts_s.length; i++) {
            fldA = stmts_s[i].id.split("_");
            channel_idx = parseInt(fldA[1]);
            cond_idx = parseInt(fldA[2]);

            target_up = document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_1").checked;
            channel_rules.push({ "on": target_up, "statements": [stmts_s[i].value] });

            if (prev_channel_idx != channel_idx) {
                if (channel_rules.length > 0) {
                    //    console.log("channel_idx:" + prev_channel_idx);
                    // console.table(channel_rules);      
                    console.log(JSON.stringify(channel_rules));
                }
                prev_channel_idx = channel_idx;
                channel_rules = [];
            }
        }

        if (channel_rules.length > 0) {
            console.log("channel_idx:" + prev_channel_idx);
            // console.table(channel_rules);      
            console.log(JSON.stringify(channel_rules));
        }

        alert("Check rules from the javascript console");
    }

    // console.log("form valid: ",$("#chFrm").valid());
    //enable before submit for posting
    let disSels = document.querySelectorAll('select[disabled="disabled"], input[disabled="disabled"]');
    for (var i = 0; i < disSels.length; i++) {
        disSels[i].disabled = false;
    }
    formSubmitting = true;


    return true;
};


var isDirty = function () { return true; }

function getVariable(variable_id) {
    for (var i = 0; i < variables.length; i++) {
        if (variables[i][0] == variable_id)
            return variables[i];
    }
    return null;
}

// Add option to a given select element
function addOption(el, value, text, selected = false) {
    var opt = document.createElement("option");
    opt.value = value;
    opt.selected = selected;
    opt.innerHTML = text
    // then append it to the select element
    el.appendChild(opt);
}



function populateStmtField(varFld, stmt = [-1, -1, 0]) {
    if (varFld.options && varFld.options.length > 0) {
        return; //already populated
    }
    fldA = varFld.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);

    document.getElementById(varFld.id.replace("var", "const")).style.display = "none";
    document.getElementById(varFld.id.replace("var", "op")).style.display = "none";

    advanced_mode = document.getElementById("mo_" + channel_idx + "_0").checked;

    addOption(varFld, -2, "delete condition");
    addOption(varFld, -1, "select", (stmt[0] == -1));

    for (var i = 0; i < variables.length; i++) {
        var type_indi = (variables[i][2] >= 50 && variables[i][2] <= 51) ? "*" : " ";
        addOption(varFld, variables[i][0], variables[i][1] + type_indi, (stmt[0] == variables[i][0]));
    }
}


//unused?
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

//called from button, 
function addStmtEVT(evt) {
    addStmt(evt.target);
}

function addStmt(elBtn, channel_idx = -1, cond_idx = 1, stmt_idx = -1, stmt = [-1, -1, 0]) {
    if (channel_idx == -1) { //from button click, no other parameters
        fldA = elBtn.id.split("_");
        channel_idx = parseInt(fldA[1]);
        cond_idx = parseInt(fldA[2]);
    }
    //get next statement index if not defined
    if (stmt_idx == -1) {
        stmt_idx = 0;
        let div_to_search = "std_" + channel_idx + "_" + cond_idx;

        let stmtDivs = document.querySelectorAll("div[id^='" + div_to_search + "']");
        // console.log("div_to_search:" + div_to_search + ", found: " + stmtDivs.length);
        for (let i = 0; i < stmtDivs.length; i++) {
            fldB = stmtDivs[i].id.split("_");
            stmt_idx = Math.max(parseInt(fldB[3]), stmt_idx);
        }
        if (stmtDivs.length >= RULE_STATEMENTS_MAX) {
            alert('Max ' + RULE_STATEMENTS_MAX + ' statements allowed');
            return false;
        }

        stmt_idx++;
        // console.log("New stmt_idx:" + stmt_idx);
    }

    suffix = "_" + channel_idx + "_" + cond_idx + "_" + (stmt_idx);
    //  console.log(suffix);

    const sel_var = createElem("select", "var" + suffix, null, "fldstmt indent", null);
    sel_var.addEventListener("change", setVar);
    const sel_op = createElem("select", "op" + suffix, null, "fldstmt", null);

    sel_op.addEventListener("change", setOper);


    const inp_const = createElem("input", "const" + suffix, 0, "fldtiny fldstmt inpnum", "text");
    const div_std = createElem("div", "std" + suffix, -2147483648, "divstament", "text");
    div_std.appendChild(sel_var);
    div_std.appendChild(sel_op);
    div_std.appendChild(inp_const);

    elBtn.parentNode.insertBefore(div_std, elBtn);
    populateStmtField(document.getElementById("var" + suffix), stmt);
}

function getVariableList() {
    $.getJSON('/data/variable-info.json', function (data) {
        variable_list = data;
    });
}

function populateTemplateSel(selEl, template_id = -1) {
    if (selEl.options && selEl.options.length > 0) {
        return; //already populated
    }
    addOption(selEl, -1, "Select template", false);
    $.getJSON('/data/template-list.json', function (data) {
        $.each(data, function (i, row) {
            addOption(selEl, row["id"], row["id"] + " - " + _ltext(row, "name"), (template_id == row["id"]));
        });
    });
}

// unused ?
function saveVal(el) {
    $.data(el, 'current', $(el).val());
}

//localised text
function _ltext(obj, prop) {
    if (obj.hasOwnProperty(prop + '_' + lang))
        return obj[prop + '_' + lang];
    else if (obj.hasOwnProperty(prop))
        return obj[prop];
    else
        return '[' + prop + ']';
}
//called from element, set by addEventlistener
function templateChangedEVT(evt) {
    console.log("templateChangedEVT");
    templateChanged(evt.target);
}

function templateChanged(selEl) {
    const fldA = selEl.id.split("_");
    channel_idx = parseInt(fldA[1]);
    //  console.log(selEl.value, channel_idx);
    template_idx = selEl.value;
    url = '/data/templates?id=' + template_idx;
    //  console.log(url, selEl.id);
    $.getJSON(url, function (data) {
        if (template_idx == -1 && confirm('Remove template definitions')) {
            deleteStmtsUI(channel_idx);
            return true;
        }
        if (!confirm('Use template ' + _ltext(data, "name") + ' - ' + _ltext(data, "desc"))) {
            $(selEl).val($.data(selEl, 'current'));
            return false;
        }
        deleteStmtsUI(channel_idx);

        $.each(data.conditions, function (cond_idx, rule) {
            document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_0").checked = !rule["on"];
            document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_1").checked = rule["on"];
            document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_0").disabled = rule["on"];
            document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_1").disabled = !rule["on"];

            elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
            $.each(rule.statements, function (j, stmt) {
                //   console.log("stmt.values:" + JSON.stringify(stmt.values));
                stmt_obj = stmt.values;
                if (stmt.hasOwnProperty('const_prompt')) {
                    stmt_obj[2] = prompt(stmt.const_prompt, stmt_obj[2]);
                }

                addStmt(elBtn, channel_idx, cond_idx, j, stmt_obj);
                var_this = get_var_by_id(stmt.values[0]);
                populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmt_obj);
            });


        });

    });

    // fixing ui fields is delayed to get dom parsed ready?
    setTimeout(function () { fillStmtRules(channel_idx, 1); }, 1000);

    return true;
}

function deleteStmtsUI(channel_idx) {
    selector_str = "div[id^='std_" + channel_idx + "']";
    document.querySelectorAll(selector_str).forEach(e => e.remove());

    // reset up/down checkboxes
    for (let cond_idx = 0; cond_idx < CHANNEL_CONDITIONS_MAX; cond_idx++) {
        if (document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_1"))
            document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_1").checked = false;
        if (document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_0"))
            document.getElementById("ctrb_" + channel_idx + "_" + cond_idx + "_0").checked = true;
    }
}


function setRuleModeEVT(evt) {
    fldA = evt.target.id.split("_");
    setRuleMode(fldA[1], fldA[2], true);
}

function setRuleMode(channel_idx, rule_mode, reset, template_id) {
    if (rule_mode == 1 || true) {
        templateSelEl = document.getElementById("rts_" + channel_idx);
        if (document.getElementById("rts_" + channel_idx))
            populateTemplateSel(templateSelEl, template_id);
        else
            console.log("Cannot find element:", "rts_" + channel_idx);
    }

    //  console.log("New rule mode:", rule_mode);
    $('#rd_' + channel_idx + ' select').attr('disabled', (rule_mode != 0));
    $('#rd_' + channel_idx + " input[type='text']").attr('disabled', (rule_mode != 0)); //jos ei iteroi?
    $('#rd_' + channel_idx + " input[type='text']").prop('readonly', (rule_mode != 0));

    $('#rd_' + channel_idx + ' .addstmtb').css({ "display": ((rule_mode != 0) ? "none" : "block") });
    $('#rts_' + channel_idx).css({ "display": ((rule_mode == 0) ? "none" : "block") });

    fillStmtRules(channel_idx, rule_mode);
    if (rule_mode == 0) //enable all
        $('#rd_' + channel_idx + " input[type='radio']").attr('disabled', false);

}

function populateOper(el, var_this, stmt = [-1, -1, 0]) {
    fldA = el.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);

    rule_mode = document.getElementById("mo_" + channel_idx + "_0").checked ? 0 : 1;

    document.getElementById(el.id.replace("op", "const")).value = stmt[2];
    // console.log("stmt[2]", stmt[2]);

    if (el.options.length == 0)
        addOption(el, -1, "select", (stmt[1] == -1));
    if (el.options)
        while (el.options.length > 1) {
            el.remove(1);
        }

    if (var_this) {
        for (let i = 0; i < opers.length; i++) {
            if (var_this[2] >= 50 && !opers[i][5]) //2-type, logical
                continue;
            if (var_this[2] < 50 && opers[i][5]) // numeric
                continue;
            const_id = el.id.replace("op", "const");
            //  console.log(const_id);
            el.style.display = "block";
            document.getElementById(el.id.replace("op", "const")).style.display = (opers[i][5]) ? "none" : "block";
            addOption(el, opers[i][0], opers[i][1], (opers[i][0] == stmt[1]));
        }
    }

    /*
        el.readonly = (!advanced_mode);
        document.getElementById(el.id.replace("op_", "var_")).readonly = (!advanced_mode);
        document.getElementById(el.id.replace("op_", "const_")).readonly = (!advanced_mode);
        */
    el.disabled = (rule_mode != 0);
    document.getElementById(el.id.replace("op_", "var_")).disabled = (rule_mode != 0);
    document.getElementById(el.id.replace("op_", "const_")).disabled = (rule_mode != 0);
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
    suffix = el.id.replace("var_", "_");
    const fldA = el.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);
    stmt_idx = parseInt(fldA[3]);
    if (el.value > -1) { //variable selected
        // let var_this = variables[el.value];
        var_this = get_var_by_id(el.value);
        populateOper(document.getElementById("op" + suffix), var_this);
    }
    else if (el.value == -2) {
        // if (!document.getElementById("var_" + channel_idx + "_" + cond_idx + "_" + stmt_idx + 2) && stmt_idx>0) { //is last
        if (confirm("Delete condition")) {
            //viimeinen olisi hyvä jättää, jos poistaa välistä niin numerot frakmentoituu
            var elem = document.getElementById("var", "std" + suffix);
            elem.parentNode.removeChild(elem);
        }
        // }
    }
}

// operator select changed, show next statement fields if hidden
function setOper(evt) {
    const el = evt.target;
    if ((el.value >= 0)) {
        fldA = el.id.split("_");
        channel_idx = parseInt(fldA[1]);
        cond_idx = parseInt(fldA[2]);
        // show initially hidden rules
        if ((cond_idx + 1) < CHANNEL_CONDITIONS_MAX) {
            document.getElementById("ru_" + channel_idx + "_" + (cond_idx + 1)).style.display = "block";
        }
    }
}


function setEnergyMeterFields(emt) { //
    idx = parseInt(emt);

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

function fillStmtRules(channel_idx, rule_mode) {
    console.log("fillStmtRules", channel_idx, rule_mode);

    prev_rule_var_defined = true;
    for (let cond_idx = 0; cond_idx < CHANNEL_CONDITIONS_MAX; cond_idx++) {
        firstStmtVarId = "var_" + channel_idx + "_" + cond_idx + "_0";
        firstStmtVar = document.getElementById(firstStmtVarId);
        first_var_defined = !!firstStmtVar;
        //if (first_var_defined)
        //    first_var_defined = (firstStmtVar.value >= 0);

        show_rule = (rule_mode == 0) || first_var_defined;
        // testing hiding , hide empty rules to save space
        if (rule_mode == 0) {
            if (cond_idx == 0)
                show_rule = true;
            else {
                show_rule = prev_rule_var_defined;
            }
        }
        prev_rule_var_defined = first_var_defined;

        document.getElementById("ru_" + channel_idx + "_" + cond_idx).style.display = (show_rule ? "block" : "none");

        if (!first_var_defined && (rule_mode == 0)) {  // advanced more add at least one statement for each rule
            elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
            //    console.log("addStmt", channel_idx, cond_idx, 0);
            addStmt(elBtn, channel_idx, cond_idx, 0);
        }
    }
}

function initChannelForm() {
    console.log("initChannelForm");
    var chtype;

    for (var channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++) {
        //ch_t_%d_1
        console.log("channel_idx:" + channel_idx, 'chty_' + channel_idx);
        //TODO: fix if more types coming
        if (document.getElementById('chty_' + channel_idx)) {
            chtype = document.getElementById('chty_' + channel_idx).value;
            setChannelFieldsByType(channel_idx, chtype);
        }
    }

    // set rule statements
    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    //  console.log("stmts_s.length:" + stmts_s.length);
    for (let i = 0; i < stmts_s.length; i++) {
        if (stmts_s[i].value) {
            fldA = stmts_s[i].id.split("_");
            channel_idx = parseInt(fldA[1]);
            cond_idx = parseInt(fldA[2]);
            //  console.log(stmts_s[i].value); 
            //    if (stmts_s[i].value) {  //TODO: miksi samat iffit sisäkkäin?
            stmts = JSON.parse(stmts_s[i].value);
            if (stmts && stmts.length > 0) {
                console.log(stmts_s[i].id + ": " + JSON.stringify(stmts));
                for (let j = 0; j < stmts.length; j++) {
                    elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
                    addStmt(elBtn, channel_idx, cond_idx, j, stmts[j]);
                    var_this = get_var_by_id(stmts[j][0]); //vika indeksi oli 1
                    populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmts[j]);
                }
            }
            // }
        }
    }

    for (let channel_idx = 0; channel_idx < CHANNEL_COUNT; channel_idx++) {
        rule_mode = channels[channel_idx]["cm"];
        template_id = channels[channel_idx]["tid"];
        console.log("rule_mode: " + rule_mode + ", template_id:" + template_id);

        if (rule_mode == 1) {
            templateSelEl = document.getElementById("rts_" + channel_idx);
            templateSelEl.value = template_id;
            //    console.log("templateSelEl.value:" + templateSelEl.value);
        }
        setRuleMode(channel_idx, rule_mode, false, template_id);
    }


    if (document.getElementById('statusauto').checked) {
        setTimeout(function () { updateStatus(true); }, 3000);
    }

}

function setChannelFieldsByType(ch, chtype) {
    for (var t = 0; t < RULE_STATEMENTS_MAX; t++) {
        divid = 'td_' + ch + "_" + t;
        var uptimediv = document.querySelector('#d_uptimem_' + ch);
        if (chtype == 0)
            uptimediv.style.display = "none";
        else
            uptimediv.style.display = "block";
    }
}


function initUrlBar(url) {
    var headdiv = document.getElementById("headdiv");


    var hdspan = document.createElement('div');
    //  hdspan.innerHTML = "Arska<br>";
    hdspan.innerHTML = "<svg viewBox='0 0 70 30' class='headlogo'><use xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='#arskalogo' id='logo' /></svg><br>";
    hdspan.classList.add("cht");
    headdiv.appendChild(hdspan);

    sections.forEach((sect, idx) => {
        /* if (url == sect.url) {
             var span = document.createElement('span');
             var b = document.createElement('b');
             b.className = 
             b.innerHTML = sect.en;
             span.appendChild(b);
             headdiv.appendChild(span);
         } 
         else { */
        var a = document.createElement('a');
        var link = document.createTextNode(sect.en);
        a.appendChild(link);
        a.title = sect.en;
        if (url == sect.url) {
            a.className = "actUrl";
        }
        a.href = sect.url;
        headdiv.appendChild(a);
        if (url == sect.url && sect.wiki) {
            var span = document.createElement('span');
            span.innerHTML = link_to_wiki(sect.wiki);
            headdiv.appendChild(span);
        }
        //}
        if (idx < sections.length - 1) {
            var sepa = document.createTextNode(" | ");
            headdiv.appendChild(sepa);
        }
    });
    // <a href="/">Dashboard</a> | <span> <b>Admin</b> </span>
}

//TODO: get from main.cpp
const force_up_hours = [0, 1, 2, 4, 8, 12, 24];
const force_up_mins = [30, 60, 120, 180, 240, 360, 480, 600, 720, 960, 1200, 1440];

function pad_to_2digits(num) {
    return num.toString().padStart(2, '0');
}


function update_schedule_select_periodical() {
    //remove starts from history
    console.log("update_schedule_select_periodical");
    let selects = document.querySelectorAll("select[id^='fupfrom_']");
    now_ts = Date.now() / 1000;
    for (i = 0; i < selects.length; i++) {
        for (j = selects[i].options.length - 1; j >= 0; j--) {
            if (selects[i].options[j].value > 0 && selects[i].options[j].value < now_ts) {
                console.log("Removing option ", j, selects[i].options[j].value);
                selects[i].remove(j);
            }
        }
    }
}



function update_fup_schedule_element(channel_idx, current_start_ts = 0) {
    //dropdown, TODO: recalculate when new hour 
    now_ts = Date.now() / 1000;
    sel_fup_from = document.getElementById("fupfrom_" + channel_idx);
    if (!sel_fup_from) {
        prev_selected = current_start_ts;
        chdiv_sched = document.getElementById("chdsched_" + channel_idx);  //chdsched_ chdinfo_
        sdiv = createElem("div", null, null, "schedsel", null);
        sel_fup_from = createElem("select", "fupfrom_" + channel_idx, null, null, null);
        sel_fup_from.name = "fupfrom_" + channel_idx;
        sdiv.insertAdjacentHTML('beforeend', 'Schedule:<br>');
        sdiv.appendChild(sel_fup_from);
        chdiv_sched.appendChild(sdiv);
    }
    else
        prev_selected = sel_fup_from.value;

    //TODO: price and so on
    $("#fupfrom_" + channel_idx).empty();
    fups_sel = document.getElementById("fups_" + channel_idx);
    duration_selected = fups_sel.value;

    if (duration_selected <= 0) {
      //  addOption(sel_fup_from, -1, "select duration", true);
        addOption(sel_fup_from, 0, "now ->", (duration_selected > 0));
        sel_fup_from.disabled = true;
        return;
    }
    sel_fup_from.disabled = false;

    first_next_hour_ts = parseInt(((Date.now() / 1000)) / 3600) * 3600 + 3600;
    start_ts = first_next_hour_ts;

    //
    addOption(sel_fup_from, 0, "now ->", (duration_selected > 0));
    cheapest_price = -VARIABLE_LONG_UNKNOWN;
    cheapest_ts = -1;
    cheapest_index = -1;
    for (k = 0; k < 24; k++) {
        end_ts = start_ts + (duration_selected * 60) - 1;
        block_price = get_price_for_block(start_ts, end_ts);
        if (block_price < cheapest_price) {
            cheapest_price = block_price;
            cheapest_ts = start_ts;
            cheapest_index = k;
        }

        if (block_price != -VARIABLE_LONG_UNKNOWN)
            price_str = "   " + block_price.toFixed(1) + " c/kWh";
        else
            price_str = "";

        addOption(sel_fup_from, start_ts, get_time_string_from_ts(start_ts).substring(0, 5) + "-> " + get_time_string_from_ts(start_ts + duration_selected * 60, true).substring(0, 5) + price_str, (prev_selected == start_ts));
        start_ts += 3600;
    }
    if (cheapest_index > -1) {
        console.log("cheapest_ts", cheapest_ts)
        sel_fup_from.value = cheapest_ts;
        sel_fup_from.options[cheapest_index + 1].innerHTML = sel_fup_from.options[cheapest_index + 1].innerHTML + " ***";
    }
}

function duration_changed(evt) {
    const fldA = evt.target.id.split("_");
    update_fup_schedule_element(fldA[1]);
}

function update_fup_duration_element(channel_idx, selected_duration_min = 60,setting_exists) {
    chdiv_sched = document.getElementById("chdsched_" + channel_idx);  //chdsched_ chdinfo_

    fups_sel = document.getElementById("fups_" + channel_idx);
    fups_val_prev = -1;
    if (fups_sel) {
        fups_val_prev = fups_sel.value;
    }
    else {
        sdiv = createElem("div", null, null, "durationsel", null);
        fups_sel = createElem("select", "fups_" + channel_idx, null, null, null);
        fups_sel.name = "fups_" + channel_idx;
        fups_sel.addEventListener("change", duration_changed);

        addOption(fups_sel, -1, "select", true); //check checked
        if (setting_exists)
            addOption(fups_sel, 0, "remove", false); //check checked
       // addOption(fups_sel, 0, "remove", false);
        for (i = 0; i < force_up_mins.length; i++) {
            min_cur = force_up_mins[i];
            duration_str = pad_to_2digits(parseInt(min_cur / 60)) + ":" + pad_to_2digits(parseInt(min_cur % 60));
            addOption(fups_sel, min_cur, duration_str, (selected_duration_min == min_cur)); //check checked
        }
        sdiv.insertAdjacentHTML('beforeend', 'Duration:<br>');
        sdiv.appendChild(fups_sel);
        chdiv_sched.appendChild(sdiv);

    }
    // now initiate value
}

function add_radiob_with_label(parent, name, value, label, checked) {
    rb_id = name + "_" + value;
    rb = createElem("input", rb_id, value, null, "radio");
    rb.name = name;
    rb.checked = checked;
    lb = createElem("label", null, null, null, null);
    lb.setAttribute("for", rb_id);
    lb.insertAdjacentText('beforeend', label);
    parent.appendChild(rb);
    parent.appendChild(lb);
    return rb;
}


function create_channel_config_elements(ce_div, channel_idx, ch_cur) {
    console.log("create_channel_config_elements", channel_idx);
    conf_div = createElem("div", null, null, null);

    //id
    id_div = createElem("div", null, null, "fldshort");
    id_div.insertAdjacentText('beforeend', 'id: ');
    inp_id = createElem("input", "id_ch_" + channel_idx, ch_cur.id_str, null, "text");
    inp_id.name = "id_ch_" + channel_idx;
    inp_id.setAttribute("maxlength", 9);
    id_div.appendChild(inp_id);
    conf_div.appendChild(id_div);

    //uptime
    ut_div = createElem("div", "d_uptimem_" + channel_idx, null, "fldtiny");
    ut_div.insertAdjacentText('beforeend', 'mininum up (s):');
    inp_ut = createElem("input", "ch_uptimem_" + channel_idx, ch_cur.uptime_minimum, null, "text");
    inp_ut.name = "ch_uptimem_" + channel_idx;
    ut_div.appendChild(inp_ut);
    conf_div.appendChild(ut_div);

    //type
    ct_div = createElem("div", null, null, "flda");
    ct_div.insertAdjacentHTML('beforeend', 'type:<br>');
    ct_sel = createElem("select", "chty_" + channel_idx, null, null);
    ct_sel.name = "chty_" + channel_idx;
    //TODO: check this
    ct_sel.addEventListener("change", setChannelFieldsEVT);

    for (var i = 0; i < channel_type_strings.length; i++) {
        is_gpio_channel = (ch_cur.gpio != 255);
        console.log("addOption", i, is_gpio_channel, ch_cur.type);
        if ((i == 0) || (i == 1 && is_gpio_channel) || !(i == 1 || is_gpio_channel)) {
            addOption(ct_sel, i, channel_type_strings[i], (ch_cur.type == i));
            console.log("addOption2", i);
        }
    }
    ct_div.appendChild(ct_sel);

    conf_div.appendChild(ct_div);

    //Mode and template selection
    rm_div = createElem("div", null, null, "secbr");
    rm_div.insertAdjacentText('beforeend', 'Rule mode:');

    rb1 = add_radiob_with_label(rm_div, "mo_" + channel_idx, 1, "Template", (ch_cur.config_mode == 1));
    rb1.addEventListener("change", setRuleModeEVT);
    rb0 = add_radiob_with_label(rm_div, "mo_" + channel_idx, 0, "Advanced", (ch_cur.config_mode == 0));
    rb0.addEventListener("change", setRuleModeEVT);

    tmpl_div = createElem("div", "rt_" + channel_idx, null, null);
    tmpl_sel = createElem("select", "rts_" + channel_idx, null, null, null);
    tmpl_sel.name = "rts_" + channel_idx;
    tmpl_div.appendChild(tmpl_sel);

    rm_div.appendChild(tmpl_div);
    tmpl_sel.addEventListener("change", templateChangedEVT);

    ct_div.appendChild(ct_sel);
    conf_div.appendChild(ct_div);

    conf_div.appendChild(rm_div); //paikka arvottu

    ce_div.appendChild(conf_div);
}


function create_channel_rule_elements(envelope_div, channel_idx, ch_cur) {
    // div envelope for all channel rules
    console.log("create_channel_rule_elements");
    rules_div = createElem("div", "rd_" + channel_idx, null, null);
    for (condition_idx = 0; condition_idx < CHANNEL_CONDITIONS_MAX; condition_idx++) {
        suffix = "_" + channel_idx + '_' + condition_idx;
        rule_div = createElem("div", "ru" + suffix, null, null);

        ruleh_div = createElem("div", null, null, "secbr");
        ruleh_div.insertAdjacentHTML('beforeend', '<br>');
        ruleh_span = createElem("span", null, null, "big");
        //TODO: match_text, get values from  config, own span/div for that so it could change whene match changes
        // match_text = s.ch[channel_idx].conditions[condition_idx].condition_active ? "<span style='color:green;'>* NOW MATCHING *</span>" : "";
        match_text = "";

        anchor = createElem("a", 'c' + channel_idx + 'r' + condition_idx);
        //  anchor.setAttribute('href', '#c'+channel_idx+'r'+condition_idx);

        anchor.insertAdjacentText('beforeend', "Rule " + (condition_idx + 1) + ":");

        ruleh_span.appendChild(anchor);
        rule_status_span = createElem("span", "rss" + suffix, null, "notify");
        if (ch_cur.active_condition_idx == condition_idx) {
           // rule_status_span.style = 'color:green';
            rule_status_span.insertAdjacentText('beforeend', "* NOW MATCHING *");
        }

        ruleh_div.appendChild(ruleh_span);
        ruleh_div.appendChild(rule_status_span);

        rulel_div = createElem("div", null, null, "secbr indent");

        ruled_div = createElem("div", 'stmtd' + suffix, null, "secbr indent");
        add_btn = createElem("input", 'addstmt' + suffix, "+", "addstmtb", "button");
        //TODO:change from hidden from based to javascript/object based
        statements = [];
        if (ch_cur.rules && ch_cur.rules[condition_idx] && ch_cur.rules[condition_idx].stmts) {
            for (j = 0; j < ch_cur.rules[condition_idx].stmts.length; j++) {
                stmt_cur = ch_cur.rules[condition_idx].stmts[j];
                // stmt_this = [stmt_cur["var"], stmt_cur["op"], stmt_cur["cfloat"]];
                stmt_this = [stmt_cur[0], stmt_cur[1], stmt_cur[3]];
                statements.push(stmt_this);
            }
        }

        stmt_hidden = createElem("input", 'stmts' + suffix, JSON.stringify(statements), "addstmtb", "hidden");
        add_btn.addEventListener("click", addStmtEVT);
        ruled_div.appendChild(add_btn);
        ruled_div.appendChild(stmt_hidden);
        ruled_div.appendChild(createElem("div", null, null, "secbr"));

        //combine rulel_div
        rulel_div.insertAdjacentText('beforeend', "Target state:");

        if (ch_cur.rules && ch_cur.rules[condition_idx] && ch_cur.rules[condition_idx].on)
            rule_on = true;
        else
            rule_on = false;
        add_radiob_with_label(rulel_div, "ctrb" + suffix, 0, "DOWN", !rule_on); //TODO:checked 
        add_radiob_with_label(rulel_div, "ctrb" + suffix, 1, "UP", rule_on); //TODO:checked

        //  snprintf(out, buff_len, "<div class='secbr'</div><div class='secbr indent'>Target state: <input type='radio' id='ctrb%s_0' name='ctrb%s' value='0' %s><label for='ctrb%s_0'>DOWN</label><input type='radio' id='ctrb%s_1' name='ctrb%s' value='1' %s><label for='ctrb%s_1'>UP</label></div>", condition_idx + 1, s.ch[channel_idx].conditions[condition_idx].condition_active ? "<span style='color:green;'>* NOW MATCHING *</span>" : "", suffix, suffix, !s.ch[channel_idx].conditions[condition_idx].on ? "checked" : "", suffix, suffix, suffix, s.ch[channel_idx].conditions[condition_idx].on ? "checked" : "", suffix);

        ruleh_div.appendChild(rulel_div);
        rule_div.appendChild(ruleh_div);
        rule_div.appendChild(ruled_div); //meniköhän oikeaan kohtaan

        rules_div.appendChild(rule_div);
    }
    envelope_div.appendChild(rules_div);
}

// dashboard or channel config (edit_mode)
/* TODO 20.7.2022
- tsekkaa erityisesti eventlistenerit ja evt versiot, niitä ei tehty loppuun
- tsekaa tuleeko stmt formiin saakka
- katso jos stmt-voisi tulla suoraan ettei tarvitse viedä formin kautta
    - pitäisikö element creation ja populate olla selkeästi erilleen, niin vasta populatessa tehdään kysely
    */
function init_channel_elements(edit_mode = false) {
    var svgns = "http://www.w3.org/2000/svg";
    var xlinkns = "http://www.w3.org/1999/xlink";

    chlist = document.getElementById("chlist");
    //async: false, because this initates elements used later, everything must be initiated, could be later split to init+populate
    $.ajax({
        url: '/export-config',
        dataType: 'json',
        async: false,
        success: function (data) {
            console.log('/export-config');
            $.each(data.ch, function (i, ch_cur) {
                console.log("init_channel_elements, loop:", i, ch_cur);
                if ((ch_cur.type == 0) && !edit_mode) {//undefined type 
                    console.log("Skipping channel " + i);
                    return;
                }

                chdiv = createElem("div", "chdiv_" + i, null, "hb");
                chdiv_head = createElem("div", null, null, "secbr cht");
                chdiv_sched = createElem("div", "chdsched_" + i, null, "secbr", null);
                chdiv_info = createElem("div", "chdinfo_" + i, null, "secbr", null);
                

                var svg = document.createElementNS(svgns, "svg");

                // svg.viewBox = '0 0 100 100';
                svg.setAttribute('viewBox', '0 0 100 100');
                svg.classList.add("statusic");
                svg.className = "statusic";

                var use = document.createElementNS(svgns, "use");
                use.id = 'status_' + i;
                use.setAttributeNS(xlinkns, "href", ch_cur.is_up ? "#green" : "#red");

                svg.appendChild(use);
                chdiv_head.appendChild(svg);

                var span = document.createElement('span');
                span.id = 'chid_' + i;
                span.innerHTML = ch_cur["id_str"];
                $('#chid_' + i).html(ch_cur["id_str"]);

                chdiv_head.appendChild(span);
                chdiv.appendChild(chdiv_head);
                chdiv.appendChild(chdiv_info);
                chdiv.appendChild(chdiv_sched);
                chdiv.appendChild(createElem("div", null, null, "secbr", null));//bottom div
                
                chlist.appendChild(chdiv);

                now_ts = Date.now() / 1000;

                if (!edit_mode) { // is dashboard
                    // fu_div = createElem("div", null, null, "secbr radio-toolbar");
                    //Set channel up for next:<br>
                    current_duration_minute = 0;
                    current_start_ts = 0;
                    has_forced_setting = false;
                    //    if ((ch_cur.force_up_until > now_ts) && (ch_cur.force_up_until - now_ts > hour_cur * 3600) && (ch_cur.force_up_until - now_ts < force_up_hours[hour_idx + 1] * 3600)) {
                    if ((ch_cur.force_up_until > now_ts)) {
                        has_forced_setting = true;
                    }

                    if ((ch_cur.force_up_from > now_ts)) {
                        current_start_ts = ch_cur.force_up_from;
                    }

                    for (hour_idx = 0; hour_idx < force_up_hours.length; hour_idx++) {
                        hour_cur = force_up_hours[hour_idx];
                        // new test
                        update_fup_duration_element(i, current_duration_minute,has_forced_setting);
                        update_fup_schedule_element(i, current_start_ts);
                        // schedule button
                        //  sched_btn = createElem("input", 'sched_' + i, " + ", "addstmtb", "button");
                        //  chdiv.appendChild(sched_btn);

                        //TODO: schedule div: info span + button (style display
                        // chdiv.appendChild(chdiv_info);
                    }
                }
                if (edit_mode) {
                    create_channel_config_elements(chdiv, i, ch_cur);
                    create_channel_rule_elements(chdiv, i, ch_cur);

                }
            });
        }
    });

}

function initWifiForm() {
    if (!wifis) {
        console.log("initWifiForm: No wifis.");
        return;
    }
    wifisp = JSON.parse(wifis);


    wifisp.sort(compare_wifis);
    var wifi_sel = document.getElementById("wifi_ssid");
    var wifi_ssid_db = document.getElementById("wifi_ssid_db");

    wifisp.forEach(function (wifi, i) {
        if (wifi.id) {
            var opt = document.createElement("option");
            opt.value = wifi.id;

            if (backup_wifi_config_mode && i == 0)
                opt.selected = true;
            else if (wifi_ssid_db.value == wifi.id) {
                opt.selected = true;
                opt.value = "NA";
            }
            opt.innerHTML = wifi.id + ' (' + wifi.rssi + ')';
            wifi_sel.appendChild(opt);
        }
    });

    /*
        wifisp.forEach(wifi => {
            if (wifi.id) {
                var opt = document.createElement("option");
                opt.value = wifi.id;
                opt.innerHTML = wifi.id + ' (' + wifi.rssi + ')';
                wifi_sel.appendChild(opt);
            }
        }); */

}

//
function initForm(url) {
    initUrlBar(url);
    if (url == '/admin') {
        initWifiForm();
        if (using_default_password) {
            document.getElementById("password_note").innerHTML = "Change your password - now using default password!"
        }
        //set timezone select element
        var timezone = document.getElementById("timezone_db").value;
        $('#timezone option').filter(function () {
            return this.value.indexOf(timezone) > -1;
        }).prop('selected', true);

        // set language select element
        var lang = document.getElementById("lang_db").value;
        $('#lang option').filter(function () {
            return this.value.indexOf(lang) > -1;
        }).prop('selected', true);
    }
    else if (url == '/channels') {
        init_channel_elements(true);
        getVariableList();
        initChannelForm();
        //scroll to anchor, done after page is created
        url_a = window.location.href.split("#");
        if (url_a.length == 2) {
            var element = document.getElementById(url_a[1]);
            if (element)
                element.scrollIntoView();
        }
    }
    /*  else if (url == '/channels_old') {
          getVariableList();
          initChannelForm();
      }
      // else if (url == '/') {
      //     setTimeout(function () { updateStatus(false); }, 2000);
      // } */
    else if (url == '/') {
        get_price_data();
        init_channel_elements(false);
      //  setTimeout(function () { get_price_data(); }, 500);
        setTimeout(function () { updateStatus(false); }, 2000);
    }

    else if (url == '/inputs') {
        if (document.getElementById("VARIABLE_SOURCE_ENABLED").value == 0) {
            variable_mode = 1;
            document.getElementById("variable_mode_0").disabled = true;
        }
        else {
            variable_mode = document.getElementById("variable_mode_db").value;
        }

        setVariableMode(variable_mode);
        document.getElementById("variable_mode_" + variable_mode).checked = true;
        setEnergyMeterFields(document.getElementById("emt").value);

        //set forecast location select element
        var location = document.getElementById("forecast_loc_db").value;
        $('#forecast_loc option').filter(function () {
            return this.value.indexOf(location) > -1;
        }).prop('selected', true);

        //set forecast location select element
        var area_code = document.getElementById("entsoe_area_code_db").value;
        if (!(area_code))
            area_code = "#";
        $('#entsoe_area_code option').filter(function () {
            return this.value.indexOf(area_code) > -1;
        }).prop('selected', true);


        // show influx definations only if enbaled by server
        if (document.getElementById("INFLUX_REPORT_ENABLED").value != 1) {
            document.getElementById("influx_div").style.display = "none";
        }
    }

    var footerdiv = document.getElementById("footerdiv");
    if (footerdiv) {
        footerdiv.innerHTML = "<br><div class='secbr'><a href='https://github.com/Netgalleria/arska-node/wiki' target='arskaw'>Arska Wiki</a> </div><div class='secbr'><i>Program version: " + VERSION + " (" + HWID + "),   Filesystem version: " + version_fs + "</i></div>";
    }
}


// for sorting wifis by signal strength
function compare_wifis(a, b) {
    return ((a.rssi > b.rssi) ? -1 : 1);
}

// cookie function, check messages read etc, https://www.w3schools.com/js/js_cookies.asp
function setCookie(cname, cvalue, exdays) {
    const d = new Date();
    d.setTime(d.getTime() + (exdays * 24 * 60 * 60 * 1000));
    let expires = "expires=" + d.toUTCString();
    document.cookie = cname + "=" + cvalue + ";" + expires + ";path=/";
}

function getCookie(cname) {
    let name = cname + "=";
    let ca = document.cookie.split(';');
    for (let i = 0; i < ca.length; i++) {
        let c = ca[i];
        while (c.charAt(0) == ' ') {
            c = c.substring(1);
        }
        if (c.indexOf(name) == 0) {
            return c.substring(name.length, c.length);
        }
    }
    return "";
}

function set_msg_read(elBtn) {
    setCookie("msg_read", Math.floor((new Date()).getTime() / 1000), 10);
    document.getElementById("msgdiv").innerHTML = "";
}



function setChannelFieldsEVT(evt) {
    setChannelFields(evt.target);
}

//combine to evt version
function setChannelFields(obj) {
    if (obj == null)
        return;
    const fldA = obj.id.split("_");
    ch = parseInt(fldA[1]);
    var chtype = obj.value;
    setChannelFieldsByType(ch, chtype);
}

// check admin form fields before form post 
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
    var op_test_gpio = document.getElementById("op_test_gpio");
    if (op_test_gpio.checked) {
        document.querySelector('#test_gpio').value = prompt("GPIO to test", "-1");
    }
    return true;
}

function doAction(action) {
    actiond_fld = document.getElementById("action");
    actiond_fld.value = action;
    save_form = document.getElementById("save_form");
    if (action == 'ts') {
        if (confirm('Syncronize device with workstation time?')) {
            document.querySelector('#ts').value = Date.now() / 1000;
            save_form.submit();
        }
    }
    else if (action == 'ota') {
        if (confirm('Backup configuration always before update! \nMove to firmware update? ')) {
            //save_form.submit();
            window.location.href = '/update';
        }
    }
    else if (action == 'reboot') {
        if (confirm('Restart the device')) {
            save_form.submit();
        }
    }
    else if (action == 'scan_wifis') {
        if (confirm('Scan WiFi networks now?')) {
            save_form.submit();
        }
    }
    else if (action == 'scan_sensors') {
        if (confirm('Scan connected temperature sensors? This can change sensor numbers, so scan only if neccessary and check sensor rules after scanning.')) {
            save_form.submit();
        }
    }
    else if (action == 'reset') {
        if (confirm('Backup configuration before reset! \nReset configuration? ')) {
            save_form.submit();
        }
    }
    else if (action == 'test_gpio') {
        document.querySelector('#test_gpio').value = prompt("GPIO to test", "-1");
        save_form.submit();

    }


}

// remove?
function clearText(elem) {
    elem.value = '';
}

