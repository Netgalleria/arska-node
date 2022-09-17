/*
Copyright, Netgalleria Oy, Olli Rinne 2021-2022
*/
sections = [{ "url": "/", "en": "Dashboard", "wiki": "Dashboard" }, { "url": "/inputs", "en": "Services", "wiki": "Edit-Services" }
    , { "url": "/channels", "id": "channels", "en": "Channels", "wiki": "Edit-Channels" }, { "url": "/admin", "en": "Admin", "wiki": "Edit-Admin" }];

let active_section_id = '';
sections_new = [
    { "id": "dashboard", "en": "Dashboard", "wiki": "Dashboard" }, { "id": "services", "en": "Services", "wiki": "Edit-Services" }
    , { "id": "channels", "en": "Channels", "wiki": "Edit-Channels" }, { "id": "admin", "en": "Admin", "wiki": "Edit-Admin" }
];
const ui_ver = 2; //transition from multi html page to 1 page ui

//constants start
//const opers = JSON.parse('%OPERS%');
//const variables = JSON.parse('%VARIABLES%');
//const CHANNEL_COUNT = parseInt('%CHANNEL_COUNT%'); //parseInt hack to prevent automatic format to mess it up
//const CHANNEL_CONDITIONS_MAX = parseInt('%CHANNEL_CONDITIONS_MAX%');
//const RULE_STATEMENTS_MAX = parseInt('%RULE_STATEMENTS_MAX%');
////const channels = JSON.parse('%channels%');  //moving data (for UI processing )  //TODO: this should come from config

//const channel_types = JSON.parse('%channel_types%');
//const hw_templates = JSON.parse('%hw_templates%'); currently hardocoded in html


//const DEBUG_MODE = ('%DEBUG_MODE%' === 'true');
//const VERSION = '%VERSION%';
//const HWID = '%HWID%';
//const VERSION_SHORT = '%VERSION_SHORT%';
//const version_fs = '%version_fs%';

//const switch_subnet_wifi = '%switch_subnet_wifi%'; //TODO: this should come from config - not used
//const lang = '%lang%';  //TODO: this should come from config
//const using_default_password = ('%using_default_password%' === 'true'); //TODO: this should come from config
//const backup_wifi_config_mode = ('%backup_wifi_config_mode%' === 'true'); //TODO: this should come from config wifi_in_setup_mode
//constants end
let g_constants = null; // from query /data/ui-constants.json
let g_config = null; // global data from export-config

const VARIABLE_LONG_UNKNOWN = -2147483648;

//selected constants
const CHANNEL_CONFIG_MODE_RULE = 0;
const CHANNEL_CONFIG_MODE_TEMPLATE = 1;

const CH_TYPE_UNDEFINED = 0;
const CH_TYPE_GPIO_FIXED = 1;
const CH_TYPE_SHELLY_1GEN = 2;
const CH_TYPE_GPIO_USER_DEF = 3;
const CH_TYPE_SHELLY_2GEN = 4;
const CH_TYPE_TASMOTA = 5;
const CH_TYPE_MODBUS_RTU = 20;
const CH_TYPE_DISABLED = 255;
const CH_TYPE_DISCOVERED = 1000; // pseudo type, use discovered device list





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
    // document.getElementById("variable_server").disabled = (variable_mode != 1);
}


function statusCBClicked(elCb) {
    if (elCb.checked) {
        setTimeout(function () { updateStatus(false); }, 300);
    }
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
    if (prices_expires > now_ts) {
        expires_in = (now_ts - prices_expires);
        console.log("Next get_price_data in" + expires_in + "seconds.");
        setTimeout(function () { get_price_data(); }, (1800 * 1000));
        return;
    }
    console.log("get_price_data starting");
    //await sleep(5000);

    $.ajax({
        url: '/data/price-data.json',
        dataType: 'json',
        async: false,
        success: function (data, textStatus, jqXHR) {
            console.log('got /data/price-data.json', textStatus, jqXHR.status);
            prices = data.prices;
            prices_first_ts = data.record_start;
            prices_resolution_min = data.resolution_m;
            prices_last_ts = prices_first_ts + (prices.length - 1) * (prices_resolution_min * 60);
            prices_expires = data.expires;
            setTimeout(function () { get_price_data(); }, 1800000);
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get prices", textStatus, jqXHR.status);
            setTimeout(function () { get_price_data(); }, 10000);
        }

    });
}

function get_price_for_block(start_ts, end_ts = 0) {
    if (prices.length == 0) { //prices (not yet) populated
        console.log("get_price_for_block, prices not populated              ");
        return -VARIABLE_LONG_UNKNOWN;
    }
    if (end_ts == 0)
        end_ts = start_ts;
    if (start_ts < prices_first_ts || end_ts > prices_last_ts) {
        return -VARIABLE_LONG_UNKNOWN;
    }
    var price_count = 0;
    var price_sum = 0;

    for (cur_ts = start_ts; cur_ts <= end_ts; cur_ts += (prices_resolution_min * 60)) {
        price_idx = (cur_ts - prices_first_ts) / (prices_resolution_min * 60);
        price_sum += prices[price_idx];
        price_count++;
    }
    var block_price_avg = (price_sum / price_count) / 1000;
    return block_price_avg;
}

function pad_to_2digits(num) {
    return num.toString().padStart(2, '0');
}

function get_time_string_from_ts(ts, show_secs = true, show_day_diff = false) {
    tmpDate = new Date(ts * 1000);
    tmpStr = tmpDate.toLocaleTimeString();

    tmpStr = pad_to_2digits(tmpDate.getHours()) + ":" + pad_to_2digits(tmpDate.getMinutes());
    if (show_secs)
        tmpStr += ":" + pad_to_2digits(tmpDate.getSeconds())

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

function update_discovered_devices() {
    $("body").css("cursor", "progress");
    $.ajax({
        url: '/discover',
        dataType: 'json',
        async: true,
        success: function (data) {
            relay_list = [];
            $.each(data.services, function (i, service) {
                //    console.log(JSON.stringify(service));
                if (service["type"] == CH_TYPE_SHELLY_1GEN || service["type"] == CH_TYPE_SHELLY_2GEN) {
                    for (i = 0; i < service.outputs; i++) {
                        relay_did = service.type + '_' + service.ip + '_' + i;
                        ip_a = service.ip.split(".");
                        relay_desc = service.app + " ." + ip_a[3];
                        //if (service.outputs > 1)
                        relay_desc = relay_desc + "/" + i;

                        relay_list.push([relay_did, relay_desc, service.type, service.ip, i]);
                        console.log(JSON.stringify([service.type, service.ip, i]));
                    }
                }
            });

            // add to type
            for (channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) {
                // clear first, TODO: could update only changed
                rtype_sel = document.getElementById("rtype_" + channel_idx);
                if (rtype_sel) { //if not yet created?
                    for (i = rtype_sel.options.length - 1; i >= 0; i--) {
                        if (rtype_sel.options[i].value.substring(0, 5) == "1000_") //discovery pseudo type
                            rtype_sel.options[i] = null;
                    }

                    if (rtype_sel.value != CH_TYPE_GPIO_FIXED) { //TODO: check if discovery on
                        for (i = 0; i < relay_list.length; i++) {
                            addOption(rtype_sel, "1000_" + relay_list[i][0], relay_list[i][1], false); //TODO:selected     
                        }
                    }
                }

            }
            $("body").css("cursor", "default");  //done

            console.log(JSON.stringify(relay_list));

        },
        fail: function () {
            console.log("Cannot get discovered devices");
        }
    });

}

// update variables and channels statuses to channels form
function updateStatus(repeat) {
    if (document.getElementById("statusauto"))
        show_variables = document.getElementById("statusauto").checked;
    else
        show_variables = false;

    update_schedule_select_periodical(); //TODO:once after hour/period change should be enough

    var jqxhr_obj = $.ajax({
        url: '/status',
        dataType: 'json',
        async: false,  //oli true
        success: function (data, textStatus, jqXHR) {
            console.log("got status data", textStatus, jqXHR.status);
            msgdiv = document.getElementById("msgdiv");
            keyfd = document.getElementById("keyfd");
            if (msgdiv) {
                if (data.last_msg_ts > getCookie("msg_read")) {
                    msgDateStr = get_time_string_from_ts(data.last_msg_ts, true, true);
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

                if (isNaN(selling)) {
                    selling_text = '';
                }
                else {
                    selling_text = (selling > 0) ? "Selling ⬆ " : "Buying ⬇ ";
                    selling_text += '<span class="big">' + Math.abs(selling) + ' W</span> (period average ' + emDate.toLocaleTimeString() + '), ';
                }
                if (isNaN(price)) {
                    price_text = 'not available';
                }
                else {
                    price_text = ' ' + price + ' ¢/kWh ';
                }

                keyfd.innerHTML = selling_text + 'Price: <span class="big">' + price_text + '</span>' + sensor_text;
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
                    id_str = ' (' + var_this[0] + ') ' + var_this[1];
                    if (var_this[2] == 50 || var_this[2] == 51) {
                        newRow = '<tr><td>' + id_str + '</td><td>' + variable.replace('"', '').replace('"', '') + '</td><td>' + variable_desc + ' (logical)</td></tr>';
                    }
                    else {
                        newRow = '<tr><td>' + id_str + '</td><td>' + variable.replace('"', '').replace('"', '') + '</td><td>' + variable_desc + ' (numeric)</td></tr>';
                    }
                    $(newRow).appendTo($("#tblVariables_tb"));
                });
                $('<tr><td>updated</td><td>' + data.localtime.substring(11) + '</td></tr></table>').appendTo($("#tblVariables_tb"));
            }

            $.each(data.ch, function (i, ch) {
                show_channel_status(i, ch)

            });
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get status", Date(Date.now()).toString(), textStatus, jqXHR.status);

            if (jqXHR.status === 401) {
                // or just location.reload();
                href_a = window.location.href.split("?");
                window.location.href = href_a[0] + "?rnd=" + Math.floor(Math.random() * 1000);
                return;
            }
        }/*,
        complete: function (jqXHR, textStatus, errorThrown) {
            console.log("A Status queried with result ", textStatus,jqXHR.status);
        }*/
    }

    );
    ;
    if (repeat)
        setTimeout(function () { updateStatus(true); }, 30000);
}


function show_channel_status(channel_idx, ch) {
    now_ts = Date.now() / 1000;
    // console.log(channel_idx,ch);
    status_el = document.getElementById("status_" + channel_idx); //.href = is_up ? "#green" : "#red";
    href = ch.is_up ? "#green" : "#red";
    if (status_el)
        status_el.setAttributeNS('http://www.w3.org/1999/xlink', 'href', href);

    info_text = "";

    if (ui_ver == 2)
        rule_link_a = " onclick='activate_section(\"channels_c" + channel_idx + "r" + ch.active_condition + "\");'";
    else
        rule_link_a = " href='/channels#c" + channel_idx + "r" + ch.active_condition + "'";

    if (ch.is_up) {
        if (ch.force_up)
            info_text += "Up based on manual schedule: " + get_time_string_from_ts(ch.force_up_from, false, true) + " --> " + get_time_string_from_ts(ch.force_up_until, false, true) + ". ";
        else if (ch.active_condition > -1)
            info_text += "Up based on <a class='chlink' " + rule_link_a + ">rule " + (ch.active_condition + 1) + "</a>. ";
    }
    else {
        if ((ch.active_condition > -1))
            info_text += "Down based on <a class='chlink' " + rule_link_a + ">rule " + (ch.active_condition + 1) + "</a>. ";
        if ((ch.active_condition == -1))
            info_text += "Down, no matching rules. ";
    }

    if (ch.force_up_from > now_ts - 100) { //leave some margin
        if (info_text)
            info_text += "<br>";
        info_text += "Scheduled: " + get_time_string_from_ts(ch.force_up_from, false, true) + " --> " + get_time_string_from_ts(ch.force_up_until, false, true);
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
    


    /*
    //TODO: in new UI model we could have names set by js element creation 
    let inputs = document.querySelectorAll("input[id^='t_'], input[id^='chty_']");
  
    for (let i = 0; i < inputs.length; i++) {
        if (inputs[i].id != inputs[i].name) {
            inputs[i].name = inputs[i].id;
            console.log("Adding name", inputs[i].name);
            names_added++;
        }
    }
    */
    
    // statements from input fields 
    // clean first storage input values
    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    for (let i = 0; i < stmts_s.length; i++) {
        stmts_s[i].value = '[]';
     /*   if (i == 0) {
            console.log(stmts_s[i].id,"-->",stmts_s[i].name)
            stmts_s[i].name = stmts_s[i].id; //send at least one field even if empty
        } */
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
            /* name should be there already
            if (saveStoreEl.name != saveStoreEl.id) {
                saveStoreEl.name = saveStoreEl.id; // only fields with a name are posted 
                console.log("Adding name", saveStoreEl.name);
                names_added++;
            }
            */
        }
    }

    // with this piece code you can capture code snipped for rulle templates
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
                prev_channel_idx = channel_idx;
                channel_rules = [];
            }
        }

        if (channel_rules.length > 0) {
            console.log("channel_idx:" + prev_channel_idx);
            //  console.log(JSON.stringify(channel_rules));
        }

        alert("Check rules from the javascript console");
    }

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
    for (var i = 0; i < g_constants.variables.length; i++) {
        if (g_constants.variables[i][0] == variable_id)
            return g_constants.variables[i];
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

    document.getElementById(varFld.id.replace("var", "const")).style.display = "none"; //const-style
    document.getElementById(varFld.id.replace("var", "op")).style.display = "none";

    advanced_mode = document.getElementById("mo_" + channel_idx + "_0").checked;

    addOption(varFld, -2, "delete condition");
    addOption(varFld, -1, "select", (stmt[0] == -1));

    for (var i = 0; i < g_constants.variables.length; i++) {
        var type_indi = (g_constants.variables[i][2] >= 50 && g_constants.variables[i][2] <= 51) ? "*" : " ";
        var id_str = g_constants.variables[i][1] + type_indi;
        addOption(varFld, g_constants.variables[i][0], id_str, (stmt[0] == g_constants.variables[i][0]));
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
    // if (value)
    if (!(value == null))
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
        for (let i = 0; i < stmtDivs.length; i++) {
            fldB = stmtDivs[i].id.split("_");
            stmt_idx = Math.max(parseInt(fldB[3]), stmt_idx);
        }
        if (stmtDivs.length >= g_constants.RULE_STATEMENTS_MAX) {
            alert('Max ' + g_constants.RULE_STATEMENTS_MAX + ' statements allowed');
            return false;
        }

        stmt_idx++;
    }

    suffix = "_" + channel_idx + "_" + cond_idx + "_" + (stmt_idx);
    //  console.log(suffix);

    const sel_var = createElem("select", "var" + suffix, null, "fldstmt indent", null);
    sel_var.addEventListener("change", setVar);
    const sel_op = createElem("select", "op" + suffix, null, "fldstmt", null);

    sel_op.addEventListener("change", setOper);

    const inp_const = createElem("input", "const" + suffix, 0, "fldtiny fldstmt inpnum", "text");
    const div_std = createElem("div", "std" + suffix, VARIABLE_LONG_UNKNOWN, "divstament", "text");
    div_std.appendChild(sel_var);
    div_std.appendChild(sel_op);
    div_std.appendChild(inp_const);

    if (elBtn.parentNode)
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
    if (obj.hasOwnProperty(prop + '_' + g_config.lang))
        return obj[prop + '_' + g_config.lang];
    else if (obj.hasOwnProperty(prop))
        return obj[prop];
    else
        return '[' + prop + ']';
}
//called from element, set by addEventlistener
function templateChangedEVT(evt) {
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

            set_radiob("ctrb_" + channel_idx + "_" + cond_idx, rule["on"] ? "1" : "0", true);

            elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
            $.each(rule.statements, function (j, stmt) {
                //   console.log("stmt.values:" + JSON.stringify(stmt.values));
                stmt_obj = stmt.values;
                if (stmt.hasOwnProperty('const_prompt')) {
                    stmt_obj[2] = prompt(stmt.const_prompt, stmt_obj[2]);
                }
                if (elBtn) {
                    addStmt(elBtn, channel_idx, cond_idx, j, stmt_obj);
                }

                var_this = get_var_by_id(stmt.values[0]);
                populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmt_obj);
            });


        });

    });

    // fixing ui fields is delayed to get dom parsed ready?
    setTimeout(function () { fillStmtRules(channel_idx, 1, template_idx); }, 1000);

    return true;
}

function deleteStmtsUI(channel_idx) {
    selector_str = "div[id^='std_" + channel_idx + "']";
    document.querySelectorAll(selector_str).forEach(e => e.remove());

    // reset up/down checkboxes
    for (let cond_idx = 0; cond_idx < g_constants.CHANNEL_CONDITIONS_MAX; cond_idx++) {
        set_radiob("ctrb_" + channel_idx + "_" + cond_idx, "0");

    }
}


function setRuleModeEVT(evt) {
    //"mo_ch_mode
    fldA = evt.target.id.split("_");
    channel_idx = fldA[1];
    rule_mode = fldA[2];
    if (rule_mode == CHANNEL_CONFIG_MODE_RULE)
        confirm_text = "Change to advanced rule mode?";
    else
        confirm_text = "Change to template mode and delete current rule definations?";

    if (confirm(confirm_text)) {
        setRuleMode(channel_idx, rule_mode, true);
        if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE)
            deleteStmtsUI(channel_idx);// delete rule statements when initiating template mode
        return true;
    }
    else {
        //back to original
        the_other_cb = document.getElementById("mo_" + channel_idx + "_" + (1 - rule_mode));
        evt.target.checked = false;
        the_other_cb.checked = true;
        return false;
    }

}

function setRuleMode(channel_idx, rule_mode, reset, template_id) {

    //  console.log("setRuleMode", template_id);
    if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE) { //template mode
        templateSelEl = document.getElementById("rts_" + channel_idx);
        if (templateSelEl) {
            populateTemplateSel(templateSelEl, template_id);
            if (reset)
                templateSelEl.value = -1;
        }
        else
            console.log("Cannot find element:", "rts_" + channel_idx);
    }


    $('#rd_' + channel_idx + ' select').attr('disabled', (rule_mode != 0));
    $('#rd_' + channel_idx + " input[type='text']").attr('disabled', (rule_mode != 0)); //jos ei iteroi?
    $('#rd_' + channel_idx + " input[type='text']").prop('readonly', (rule_mode != 0));

    $('#rd_' + channel_idx + ' .addstmtb').css({ "display": ((rule_mode != 0) ? "none" : "block") });
    $('#rts_' + channel_idx).css({ "display": ((rule_mode == CHANNEL_CONFIG_MODE_RULE) ? "none" : "block") });

    fillStmtRules(channel_idx, rule_mode, template_id);
    if (rule_mode == CHANNEL_CONFIG_MODE_RULE) //enable all
        $('#rd_' + channel_idx + " input[type='radio']").attr('disabled', false);
}


function populateOper(el_oper, var_this, stmt = [-1, -1, 0]) {
    fldA = el_oper.id.split("_");
    channel_idx = parseInt(fldA[1]);
    cond_idx = parseInt(fldA[2]);

    rule_mode = document.getElementById("mo_" + channel_idx + "_0").checked ? 0 : 1;

    el_const = document.getElementById(el_oper.id.replace("op", "const"));
    el_const.value = stmt[2];

    if (el_oper.options.length == 0)
        addOption(el_oper, -1, "select", (stmt[1] == -1));
    if (el_oper.options)
        while (el_oper.options.length > 1) {
            el_oper.remove(1);
        }

    if (var_this) {
        //populate oper select
        for (let i = 0; i < g_constants.opers.length; i++) {
            if (g_constants.opers[i][6]) //boolean variable, defined/undefined oper is shown for all variables
                void (0); // do nothing, do not skip
            else if (var_this[2] >= 50 && !g_constants.opers[i][5]) //boolean variable, not boolean oper
                continue;
            else if (var_this[2] < 50 && g_constants.opers[i][5]) // numeric variable, boolean oper
                continue;
            const_id = el_oper.id.replace("op", "const");
            el_oper.style.display = "block";
            // constant element visibility

            addOption(el_oper, g_constants.opers[i][0], g_constants.opers[i][1], (g_constants.opers[i][0] == stmt[1]));
            if (g_constants.opers[i][0] == stmt[1]) {
                el_const.style.display = (g_constants.opers[i][5] || g_constants.opers[i][6]) ? "none" : "block"; //const-style    
            }
        }
    }

    el_oper.disabled = (rule_mode != 0);
    document.getElementById(el_oper.id.replace("op_", "var_")).disabled = (rule_mode != 0);
    document.getElementById(el_oper.id.replace("op_", "const_")).disabled = (rule_mode != 0);
}

function get_var_by_id(id) {
    for (var i = 0; i < g_constants.variables.length; i++) {
        if (g_constants.variables[i][0] == id) {
            return g_constants.variables[i];
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
        var_this = get_var_by_id(el.value);
        populateOper(document.getElementById("op" + suffix), var_this);
    }
    else if (el.value == -2) {
        var elem = document.getElementById("std" + suffix);
        if (elem)
            elem.parentNode.removeChild(elem);
    }
}



// operator select changed, show next statement fields if hidden
function setOper(evt) {
    const el_oper = evt.target;
    if ((el_oper.value >= 0)) {
        fldA = el_oper.id.split("_");
        channel_idx = parseInt(fldA[1]);
        cond_idx = parseInt(fldA[2]);
        // show initially hidden rules
        if ((cond_idx + 1) < g_constants.CHANNEL_CONDITIONS_MAX) {
            document.getElementById("ru_" + channel_idx + "_" + (cond_idx + 1)).style.display = "block";
        }
        // set constant visibility (defined oper use no constants)
        console.log("setOper", el_oper.id, el_oper.value);
        el_const = document.getElementById(el_oper.id.replace("op", "const"));
        el_const.style.display = (g_constants.opers[el_oper.value][5] || g_constants.opers[el_oper.value][6]) ? "none" : "block"; // const-style
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

// populate rule statements of a channel
function fillStmtRules(channel_idx, rule_mode, template_id) {

    prev_rule_var_defined = true;
    for (let cond_idx = 0; cond_idx < g_constants.CHANNEL_CONDITIONS_MAX; cond_idx++) {
        firstStmtVarId = "var_" + channel_idx + "_" + cond_idx + "_0";
        firstStmtVar = document.getElementById(firstStmtVarId);
        first_var_defined = !!firstStmtVar;

        show_rule = (rule_mode == CHANNEL_CONFIG_MODE_RULE) || first_var_defined;

        if (!template_id) {// not yet populated to the select
            console.log("template_id from ui select");
            template_id = document.getElementById("rts_" + channel_idx).value;
        }

        if (rule_mode == CHANNEL_CONFIG_MODE_RULE) {
            if (cond_idx == 0)
                show_rule = true;
            else {
                show_rule = prev_rule_var_defined;
            }
        }
        else if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE)
            if (template_id == -1) //template mode, no template id selected
                show_rule = false;
            else {
                show_rule = first_var_defined;
            }

        prev_rule_var_defined = first_var_defined;

        //rule defination inside this div
        ru_div = document.getElementById("ru_" + channel_idx + "_" + cond_idx);
        if (ru_div)
            ru_div.style.display = (show_rule ? "block" : "none");

        if (!first_var_defined && (rule_mode == CHANNEL_CONFIG_MODE_RULE)) {  // advanced more add at least one statement for each rule
            elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
            if (elBtn)
                addStmt(elBtn, channel_idx, cond_idx, 0);
        }
    }
}

function initChannelForm() {
    console.log("initChannelForm");
    /*
    var chtype;

    let count = 0;
    for (var channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) {
        //TODO: fix if more types coming
        if (document.getElementById('chty_' + channel_idx)) {
            count++;
            chtype = document.getElementById('chty_' + channel_idx).value;
            setChannelFieldsByType(channel_idx, chtype);
            console.log("calling  setChannelFieldsByType");
        }
    }
    alert("setChannelFieldsByType calls " + count);
    */

    // set rule statements, statements are in hidden fields
    let stmts_s = document.querySelectorAll("input[id^='stmts_']");
    for (let i = 0; i < stmts_s.length; i++) {
        if (stmts_s[i].value) {
            fldA = stmts_s[i].id.split("_");
            channel_idx = parseInt(fldA[1]);
            cond_idx = parseInt(fldA[2]);
            //  console.log(stmts_s[i].value); 

            stmts = JSON.parse(stmts_s[i].value);
            if (stmts && stmts.length > 0) {
                console.log(stmts_s[i].id + ": " + JSON.stringify(stmts));
                for (let j = 0; j < stmts.length; j++) {
                    elBtn = document.getElementById("addstmt_" + channel_idx + "_" + cond_idx);
                    if (elBtn) {
                        addStmt(elBtn, channel_idx, cond_idx, j, stmts[j]);
                    }
                    var_this = get_var_by_id(stmts[j][0]); //vika indeksi oli 1
                    populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmts[j]);
                }
            }
        }
    }

    for (let channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) {
        //rule_mode = channels[channel_idx]["cm"];
        //template_id = channels[channel_idx]["tid"];
        // now from global variale updated in the first query
        rule_mode = g_config.ch[channel_idx]["config_mode"];
        template_id = g_config.ch[channel_idx]["template_id"];

        console.log("rule_mode: " + rule_mode + ", template_id:" + template_id);

        if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE) {
            templateSelEl = document.getElementById("rts_" + channel_idx);
            templateSelEl.value = template_id;
        }
        setRuleMode(channel_idx, rule_mode, false, template_id);
    }

}

function is_relay_id_used(channel_type) { // id required
    return [CH_TYPE_GPIO_FIXED, CH_TYPE_GPIO_USER_DEF, CH_TYPE_MODBUS_RTU].includes(parseInt(channel_type));
}
function is_relay_ip_used(channel_type) { //ip required
    return [CH_TYPE_SHELLY_1GEN, CH_TYPE_SHELLY_2GEN, CH_TYPE_TASMOTA].includes(parseInt(channel_type));
}
function is_relay_uid_used(channel_type) { //unit_id required
    if (is_relay_ip_used(parseInt(channel_type)))
        return true;
    return [CH_TYPE_MODBUS_RTU].includes(parseInt(channel_type));
}

function set_relay_field_visibility(channel_idx, chtype) {
    document.getElementById("d_rip_" + channel_idx).style.display = is_relay_ip_used(chtype) ? "block" : "none";
    document.getElementById("d_rid_" + channel_idx).style.display = is_relay_id_used(chtype) ? "block" : "none";
    document.getElementById("d_ruid_" + channel_idx).style.display = is_relay_uid_used(chtype) ? "block" : "none";
    relay_id_caption_span = document.getElementById("ch_ridcap_" + channel_idx);

    if (is_relay_id_used(chtype)) {
        max_rid = (chtype == (CH_TYPE_GPIO_USER_DEF) ? 39 : 255); //GPIO max 39
        document.getElementById("ch_rid_" + channel_idx).setAttribute("max", max_rid);
    }
    if (is_relay_uid_used(chtype)) {
        min_ruid = (chtype == (CH_TYPE_TASMOTA) ? 1 : 0); //Shelly min 0, Tasmota 1
        cur_ruid = document.getElementById("ch_ruid_" + channel_idx).value;
        document.getElementById("ch_ruid_" + channel_idx).setAttribute("min", min_ruid);
        document.getElementById("ch_ruid_" + channel_idx).value = Math.max(min_ruid, cur_ruid); //for min value
    }

    if (chtype == CH_TYPE_GPIO_USER_DEF) {
        relay_id_caption_span.innerHTML = "gpio:<br>";
    }
    else if (chtype == CH_TYPE_GPIO_FIXED) {
        relay_id_caption_span.innerHTML = "gpio (fixed):<br>";
        $('#ch_rid_' + channel_idx).attr('disabled', true);
    }
    else
        relay_id_caption_span.innerHTML = "device id:"; //TODO: later modbus...
}

function setChannelFieldsByType(channel_idx, chtype_in) {
    if (chtype_in.substring(0, 5) == "1000_") { //combined id, like 1000_2_192.168.66.36_0 
        id_a = chtype_in.split("_");
        chtype = id_a[1];
        if (is_relay_ip_used(chtype))
            document.getElementById("ch_rip_" + channel_idx).value = id_a[2];
        if (is_relay_uid_used(chtype))
            document.getElementById("ch_ruid_" + channel_idx).value = id_a[3];
        //       console.log(id_a,channel_idx,chtype,is_relay_ip_used(chtype));
    }
    else
        chtype = chtype_in;

    set_relay_field_visibility(channel_idx, chtype);

    chtype = chtype_in;
    for (var t = 0; t < g_constants.RULE_STATEMENTS_MAX; t++) {
        $('#d_rc1_' + channel_idx + ' input').attr('disabled', (chtype == CH_TYPE_UNDEFINED));
    }
}

function set_url_bar(section_id) {
    var headdiv = document.getElementById("headdiv");
    headdiv.innerHTML = ""; // clear div first
    var hdspan = document.createElement('div');
    hdspan.innerHTML = "<svg viewBox='0 0 70 30' class='headlogo'><use xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='#arskalogo' id='logo' /></svg><br>";
    hdspan.classList.add("cht");
    headdiv.appendChild(hdspan);

    sections_new.forEach((sect, idx) => {
        var a = document.createElement('a');
        var link = document.createTextNode(sect.en);
        a.appendChild(link);
        a.title = sect.en;
        if (section_id == sect.id) {
            a.className = "actUrl";
        }
        // a.href = sect.url;
        a.setAttribute("onclick", "activate_section('" + sect.id + "');");

        headdiv.appendChild(a);

        if (section_id == sect.id && sect.wiki) {
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
}
function initUrlBar(url) {
    var headdiv = document.getElementById("headdiv");
    // headdiv.innerHTML = ""; // clear div first
    var hdspan = document.createElement('div');
    hdspan.innerHTML = "<svg viewBox='0 0 70 30' class='headlogo'><use xmlns:xlink='http://www.w3.org/1999/xlink' xlink:href='#arskalogo' id='logo' /></svg><br>";
    hdspan.classList.add("cht");
    headdiv.appendChild(hdspan);

    sections.forEach((sect, idx) => {
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
}

//TODO: get from main.cpp
const force_up_hours = [0, 1, 2, 4, 8, 12, 24];
const force_up_mins = [30, 60, 120, 180, 240, 360, 480, 600, 720, 960, 1200, 1440];



function update_schedule_select_periodical() {
    //remove schedule start times from history
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

    chdiv_sched = document.getElementById("chdsched_" + channel_idx);  
    if (!chdiv_sched) //undefined, not on dashboard
        return;
   
    if (!sel_fup_from) {
        prev_selected = current_start_ts;
        sdiv = createElem("div", null, null, "schedsel", null);
        sel_fup_from = createElem("select", "fupfrom_" + channel_idx, null, null, null);
        sel_fup_from.name = "fupfrom_" + channel_idx;
        sdiv.insertAdjacentHTML('beforeend', 'Schedule:<br>');
        sdiv.appendChild(sel_fup_from);

        // update button
        sdiv_btn = createElem("div", null, null, "shedupdv", null);
        fup_btn = createElem("input", "fupbtn" + channel_idx, "💾", "fupbtn", "button");
        fup_btn.setAttribute("onclick", "schedule_update('" + channel_idx + "');");
        fup_btn.disabled = true; //wait for changes

        sdiv_btn.insertAdjacentHTML('beforeend', ' <br>');
        sdiv_btn.appendChild(fup_btn);

       // console.log("chdsched_" + channel_idx);
        if (chdiv_sched) {
            chdiv_sched.appendChild(sdiv);
            chdiv_sched.appendChild(sdiv_btn);
        }
    }
    else
        prev_selected = sel_fup_from.value;

    //TODO: price and so on
    $("#fupfrom_" + channel_idx).empty();
    fupdur_sel = document.getElementById("fupdur_" + channel_idx);

    duration_selected = fupdur_sel.value;

    if (duration_selected <= 0) {
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

        addOption(sel_fup_from, start_ts, get_time_string_from_ts(start_ts, false, true) + "-> " + get_time_string_from_ts(start_ts + duration_selected * 60, false, true) + price_str, (prev_selected == start_ts));
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
    channel_idx = fldA[1];
    update_fup_schedule_element(channel_idx);
    document.getElementById("fupbtn" + channel_idx).disabled = false;
    
}

//TODO: refaktoroi myös muut
function remove_select_options(select_element) {
    for(i = select_element.options.length - 1; i >= 0; i--) {
        select_element.remove(i);
     }
}
function update_fup_duration_element(channel_idx, selected_duration_min = 60, setting_exists) {
    chdiv_sched = document.getElementById("chdsched_" + channel_idx);  //chdsched_ chdinfo_
    if (!chdiv_sched)
        return;

    fupdur_sel = document.getElementById("fupdur_" + channel_idx);
    fups_val_prev = -1;
    if (fupdur_sel) {
        fups_val_prev = fupdur_sel.value;
        remove_select_options(fupdur_sel);
    }
    else {
        sdiv = createElem("div", null, null, "durationsel", null);
        fupdur_sel = createElem("select", "fupdur_" + channel_idx, null, null, null);
        fupdur_sel.name = "fupdur_" + channel_idx;
        fupdur_sel.addEventListener("change", duration_changed);
        sdiv.insertAdjacentHTML('beforeend', 'Duration:<br>');
        sdiv.appendChild(fupdur_sel);
    //    console.log("chdsched_" + channel_idx);
        if (chdiv_sched)
            chdiv_sched.appendChild(sdiv);
//select population was here
        
    }
    addOption(fupdur_sel, -1, "select", true); //check checked
    if (setting_exists)
        addOption(fupdur_sel, 0, "unschedule", false); //check checked
    for (i = 0; i < force_up_mins.length; i++) {
        min_cur = force_up_mins[i];
        duration_str = pad_to_2digits(parseInt(min_cur / 60)) + ":" + pad_to_2digits(parseInt(min_cur % 60));
        addOption(fupdur_sel, min_cur, duration_str, (selected_duration_min == min_cur)); //check checked
    }

    // now initiate value
}

// add radio button and label, set checked/disabled
function add_radiob_with_label(parent, name, value, label, checked, readonly = false) {
    rb_id = name + "_" + value;
    rb = createElem("input", rb_id, value, null, "radio");
    rb.name = name;
    rb.checked = checked;
    if (!checked && readonly) {
        rb.disabled = true;
    }
    lb = createElem("label", null, null, null, null);
    lb.setAttribute("for", rb_id);
    lb.insertAdjacentText('beforeend', label);
    parent.appendChild(rb);
    parent.appendChild(lb);
    return rb;
}
// set radio button checked and the others in a group disabled if readonly
function set_radiob(prefix, value, readonly) {
    rb_id = prefix + "_" + value;
    // select all radio buttons in the group
    let rbs = document.querySelectorAll("input[id^='" + prefix + "_']");
    for (let i = 0; i < rbs.length; i++) {
        if (rbs[i].id == rb_id) {
            rbs[i].checked = true;
            rbs[i].disabled = false;
        }
        else if (readonly) {
            rbs[i].disabled = true;
        }
    }
}


function create_channel_config_elements(ce_div, channel_idx, ch_cur) {
    conf_div = createElem("div", null, null, null);

    rc0_div = createElem("div", null, null, "secbr"); // first config row
    rc1_div = createElem("div", "d_rc1_" + channel_idx, null, "secbr"); // 2nd row

    //id
    id_div = createElem("div", null, null, "fldshort");
    id_div.insertAdjacentText('beforeend', 'id: ');
    inp_id = createElem("input", "id_ch_" + channel_idx, ch_cur.id_str, null, "text");
    inp_id.name = "id_ch_" + channel_idx;
    inp_id.setAttribute("maxlength", 9);
    id_div.appendChild(inp_id);
    rc0_div.appendChild(id_div);
    //conf_div.appendChild(id_div);

    //uptime
    upt_div = createElem("div", "d_uptimem_" + channel_idx, null, "fldtiny");
    upt_div.insertAdjacentText('beforeend', 'minim. up(s):');
    inp_ut = createElem("input", "ch_uptimem_" + channel_idx, ch_cur.uptime_minimum, null, "text");
    inp_ut.name = "ch_uptimem_" + channel_idx;
    upt_div.appendChild(inp_ut);
    rc0_div.appendChild(upt_div); //oli rc1_div

    //relay type
    relayt_div = createElem("div", "rtyped_" + channel_idx, null, "flda");
    relayt_div.insertAdjacentHTML('beforeend', 'relay type:<br>');
    ct_sel = createElem("select", "rtype_" + channel_idx, null, null);
    ct_sel.name = "rtype_" + channel_idx;
    ct_sel.addEventListener("change", setChannelFieldsEVT);

    is_fixed_gpio_channel = (ch_cur.type == CH_TYPE_GPIO_FIXED);
    //  if (!is_fixed_gpio_channel)
    //      addOption(ct_sel, CH_TYPE_DISCOVERED, "discovered", false); //TODO: when this could be checked

    for (var i = 0; i < g_constants.channel_types.length; i++) {
        if ((!is_fixed_gpio_channel && g_constants.channel_types[i].id != CH_TYPE_GPIO_FIXED) || (is_fixed_gpio_channel && g_constants.channel_types[i].id == CH_TYPE_GPIO_FIXED)) {
            addOption(ct_sel, g_constants.channel_types[i].id, g_constants.channel_types[i].name, (ch_cur.type == g_constants.channel_types[i].id));
        }
    }
    relayt_div.appendChild(ct_sel);
    rc0_div.appendChild(relayt_div); //oli rc0_div

    //Rule config div
    //maybe one row div including uptime
    rid_div = createElem("div", "d_rid_" + channel_idx, null, "flda"); //"fldtiny"
    relay_id_caption_span = createElem("span", "ch_ridcap_" + channel_idx, null, null);
    relay_id_caption_span.innerHTML = "device id:<br>";
    // relay id, modbus etc... RFU
    inp_rid = createElem("input", "ch_rid_" + channel_idx, ch_cur.r_id, "flda", "number");
    inp_rid.name = "ch_rid_" + channel_idx;
    inp_rid.setAttribute("maxlength", 3);
    inp_rid.setAttribute("min", 0);
    inp_rid.setAttribute("max", 255);
    inp_rid.setAttribute("step", 1);
    inp_rid.setAttribute("size", 3);
    inp_rid.setAttribute("pattern", "^([0-9]|[0-9][0-9]|2[0-4][0-9]|25[0-5])$");

    // relay ip address
    rip_div = createElem("div", "d_rip_" + channel_idx, null, "flda");
    relay_ip_caption_span = createElem("span", "ch_ripcap_" + channel_idx, null, null);
    relay_ip_caption_span.innerHTML = "IP address:<br>";
    inp_rip = createElem("input", "ch_rip_" + channel_idx, ch_cur.r_ip, "flda", "text");
    inp_rip.name = "ch_rip_" + channel_idx;
    inp_rip.setAttribute("minlength", 7);
    inp_rip.setAttribute("maxlength", 15);
    inp_rip.setAttribute("size", 15);
    inp_rip.setAttribute("placeholder", "xxx.xxx.xxx.xxx");
    inp_rip.setAttribute("pattern", "^([0-9]{1,3}\.){3}[0-9]{1,3}$");

    // relay unit id
    ruid_div = createElem("div", "d_ruid_" + channel_idx, null, "flda");
    relay_uid_caption_span = createElem("span", "ch_ruidcap_" + channel_idx, null, null);
    relay_uid_caption_span.innerHTML = "relay id:<br>";
    inp_ruid = createElem("input", "ch_ruid_" + channel_idx, ch_cur.r_uid, "flda", "number");
    inp_ruid.name = "ch_ruid_" + channel_idx;
    inp_ruid.setAttribute("min", 0);
    inp_ruid.setAttribute("max", 255);
    inp_ruid.setAttribute("step", 1);
    inp_ruid.setAttribute("pattern", "^([0-9]|[0-9][0-9]|2[0-4][0-9]|25[0-5])$");


    rid_div.appendChild(relay_id_caption_span);
    rid_div.appendChild(inp_rid);

    rip_div.appendChild(relay_ip_caption_span);
    rip_div.appendChild(inp_rip);

    ruid_div.appendChild(relay_uid_caption_span);
    ruid_div.appendChild(inp_ruid);

    rc1_div.appendChild(rid_div);
    rc1_div.appendChild(rip_div);
    rc1_div.appendChild(ruid_div);

    conf_div.appendChild(rc0_div);
    conf_div.appendChild(rc1_div);

    //Mode and template selection
    rm_div = createElem("div", null, null, "secbr");
    rm_div.insertAdjacentText('beforeend', 'Rule mode:');

    rb1 = add_radiob_with_label(rm_div, "mo_" + channel_idx, 1, "Template", (ch_cur.config_mode == CHANNEL_CONFIG_MODE_TEMPLATE), false);
    rb1.addEventListener("change", setRuleModeEVT);
    rb0 = add_radiob_with_label(rm_div, "mo_" + channel_idx, 0, "Advanced", (ch_cur.config_mode == CHANNEL_CONFIG_MODE_RULE), false);
    rb0.addEventListener("change", setRuleModeEVT);

    tmpl_div = createElem("div", "rt_" + channel_idx, null, null);
    tmpl_sel = createElem("select", "rts_" + channel_idx, null, null, null);
    tmpl_sel.name = "rts_" + channel_idx;
    tmpl_div.appendChild(tmpl_sel);

    rm_div.appendChild(tmpl_div);
    tmpl_sel.addEventListener("change", templateChangedEVT);

    // relayt_div.appendChild(ct_sel);
    // conf_div.appendChild(relayt_div);

    conf_div.appendChild(rm_div); //paikka arvottu
    ce_div.appendChild(conf_div);

    // visibility depend on the type
    set_relay_field_visibility(channel_idx, ch_cur.type);
}


function create_channel_rule_elements(envelope_div, channel_idx, ch_cur) {
    // div envelope for all channel rules
    rules_div = createElem("div", "rd_" + channel_idx, null, null);
    for (condition_idx = 0; condition_idx < g_constants.CHANNEL_CONDITIONS_MAX; condition_idx++) {
        suffix = "_" + channel_idx + '_' + condition_idx;
        rule_div = createElem("div", "ru" + suffix, null, null);

        ruleh_div = createElem("div", null, null, "secbr");
        ruleh_div.insertAdjacentHTML('beforeend', '<br>');
        ruleh_span = createElem("span", null, null, "big");
        match_text = "";

        anchor = createElem("a", 'c' + channel_idx + 'r' + condition_idx);

        anchor.insertAdjacentText('beforeend', "Rule " + (condition_idx + 1) + ":");

        ruleh_span.appendChild(anchor);
        rule_status_span = createElem("span", "rss" + suffix, null, "notify");
        /* now disabled to prevent expired info while editing
        if (ch_cur.active_condition_idx == condition_idx) {
            rule_status_span.insertAdjacentText('beforeend', "* NOW MATCHING *");
        }
        */

        ruleh_div.appendChild(ruleh_span);
        ruleh_div.appendChild(rule_status_span);

        rulel_div = createElem("div", null, null, "secbr indent");

        ruled_div = createElem("div", 'stmtd' + suffix, null, "secbr"); //indent
        add_btn = createElem("input", 'addstmt' + suffix, "+", "addstmtb", "button");
        //TODO:change from hidden from based to javascript/object based
        statements = [];
        if (ch_cur.rules && ch_cur.rules[condition_idx] && ch_cur.rules[condition_idx].stmts) {
            for (j = 0; j < ch_cur.rules[condition_idx].stmts.length; j++) {
                stmt_cur = ch_cur.rules[condition_idx].stmts[j];
                stmt_this = [stmt_cur[0], stmt_cur[1], stmt_cur[3]];
                statements.push(stmt_this);
            }
        }

        stmt_hidden = createElem("input", "stmts" + suffix, JSON.stringify(statements), "addstmtb", "hidden");
        stmt_hidden.name = "stmts" + suffix;// experimental
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
        add_radiob_with_label(rulel_div, "ctrb" + suffix, 0, "DOWN", !rule_on, true); //TODO:checked 
        add_radiob_with_label(rulel_div, "ctrb" + suffix, 1, "UP", rule_on, true); //TODO:checked
        /* under construction
                if (true) { //suffix = "_" + channel_idx + '_' + condition_idx;
                    console.log("ctrb" + suffix + "_0");
                    console.log(document.getElementById("ctrb" + suffix + "_0"));
                    document.getElementById("ctrb" + suffix + "_0").disabled = rule_on;
                    document.getElementById("ctrb" + suffix + "_1").disabled = !rule_on;  
                }
                */

        ruleh_div.appendChild(rulel_div);
        rule_div.appendChild(ruleh_div);
        rule_div.appendChild(ruled_div); //meniköhän oikeaan kohtaan

        rules_div.appendChild(rule_div);
    }
    envelope_div.appendChild(rules_div);
}

// dashboard or channel config (edit_mode)
/* TODO 20.7.2022
- katso jos stmt-voisi tulla suoraan ettei tarvitse viedä formin kautta
    - pitäisikö element creation ja populate olla selkeästi erilleen, niin vasta populatessa tehdään kysely
    */
function init_channel_elements(edit_mode = false) {
    var svgns = "http://www.w3.org/2000/svg";
    var xlinkns = "http://www.w3.org/1999/xlink";
    // id namespace to prevent id duplicates
    var idns = edit_mode ? "e" : "d";

    // new and old mixed
    if (edit_mode)
        chlist = document.getElementById("channels_chlist");
    else
        chlist = document.getElementById("dashboard_chlist");
    if (!chlist) // old ui
        chlist = document.getElementById("chlist");

    listed_channels = 0;

    $.each(g_config.ch, function (i, ch_cur) {
        if ((ch_cur.type == CH_TYPE_UNDEFINED) && !edit_mode) {//undefined type 
            console.log("Skipping channel " + i);
            return;
        }
        listed_channels++;

        chdiv = createElem("div", idns + "chdiv_" + i, null, "hb");
        chdiv_head = createElem("div", null, null, "secbr cht");

        if (!edit_mode) { //experimental
            chdiv_sched = createElem("div", "chdsched_" + i, null, "secbr", null);
            chdiv_info = createElem("div", "chdinfo_" + i, null, "secbr", null);
            var svg = document.createElementNS(svgns, "svg");
            svg.setAttribute('viewBox', '0 0 100 100');
            svg.classList.add("statusic");
            svg.className = "statusic";
            var use = document.createElementNS(svgns, "use");
            use.id = 'status_' + i;
            use.setAttributeNS(xlinkns, "href", ch_cur.is_up ? "#green" : "#red");
            svg.appendChild(use);
            chdiv_head.appendChild(svg);
        }
        else {
            chdiv_info = null;
            chdiv_sched = null;
        }

        // namespace?
        var span = document.createElement('span');
        span.id = idns + 'chid_' + i;
        // idns, id, naspace
        span.innerHTML = ch_cur["id_str"];
        // $('#'+idns+'chid_' + i).html(ch_cur["id_str"]);
        chdiv_head.appendChild(span);

        chdiv.appendChild(chdiv_head);
        if (chdiv_info)
            chdiv.appendChild(chdiv_info);
        if (chdiv_sched)
            chdiv.appendChild(chdiv_sched);

        chdiv.appendChild(createElem("div", null, null, "secbr", null));//bottom div

        chlist.appendChild(chdiv);

        now_ts = Date.now() / 1000;

        if (!edit_mode) { // is dashboard
            //Set channel up for next:<br>
            current_duration_minute = 0;
            current_start_ts = 0;
            has_forced_setting = false;
            if ((ch_cur.force_up_until > now_ts)) {
                has_forced_setting = true;
            }

            if ((ch_cur.force_up_from > now_ts)) {
                current_start_ts = ch_cur.force_up_from;
            }

            //experimental, is this enough or do we need loop
            update_fup_duration_element(i, current_duration_minute, has_forced_setting);
            update_fup_schedule_element(i, current_start_ts);

           /* for (hour_idx = 0; hour_idx < force_up_hours.length; hour_idx++) {
                update_fup_duration_element(i, current_duration_minute, has_forced_setting);
                update_fup_schedule_element(i, current_start_ts);
                ;
            } */
        }
        if (edit_mode) {
            create_channel_config_elements(chdiv, i, ch_cur);
            create_channel_rule_elements(chdiv, i, ch_cur);
        }
    });
    if (listed_channels == 0) {
        btnSubmit = document.getElementById("btnSubmit");
        if (btnSubmit)
            btnSubmit.style.display = "none";
        chlist.insertAdjacentHTML('beforeend', "<br><span>No channels defined. Define them on <a href=\"/channels\">Channels section</a></span>");
    }

}

function initWifiForm() {
    var wifisp = null;
    $.ajax({
        url: '/data/wifis.json',
        dataType: 'json',
        async: false,
        success: function (data) { wifisp = data; console.log("got wifis"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get wifis", textStatus, jqXHR.status);
        }
    });

    if (!wifisp) {
        console.log("initWifiForm: No wifis.");
        return;
    }
    //  wifisp = JSON.parse(wifis);
    wifisp.sort(compare_wifis);
    var wifi_sel = document.getElementById("wifi_ssid");
    var wifi_ssid_db = g_config.wifi_ssid;// document.getElementById("wifi_ssid_db");

    wifisp.forEach(function (wifi, i) {
        if (wifi.id) {
            var opt = document.createElement("option");
            opt.value = wifi.id;

            if (g_config.wifi_in_setup_mode && i == 0)
                opt.selected = true;
            else if (wifi_ssid_db.value == wifi.id) {
                opt.selected = true;
                opt.value = "NA";
            }
            opt.innerHTML = wifi.id + ' (' + wifi.rssi + ')';
            wifi_sel.appendChild(opt);
        }
    });
}
function post_schedule_update(channel_idx = -1) {
    console.log("post_schedule_update");
    var scheds = [];

    for (i = 0; i < g_constants.CHANNEL_COUNT; i++) {
        if (channel_idx != -1 && i != channel_idx)
            continue;
        sel1 = document.getElementById("fupdur_" + i);
        if (sel1) {
            sel2 = document.getElementById("fupfrom_" + i);
            if (sel1.value > -1)
                scheds.push({ "ch_idx": i, "duration": sel1.value, "from": sel2.value });
        }

    }
    console.log(scheds);
    console.log(JSON.stringify(scheds));
    $.ajax({
        type: "POST",
        url: "/update.schedule",
        async : "false",
        data: JSON.stringify({ schedules: scheds }),
        contentType: "application/json; charset=utf-8",
        dataType: "json",
        success: function (data) { console.log("success", data); },
        error: function (errMsg) {
            console.log(errMsg);
        }
    });
}
// single channel schedule iupdate
function schedule_update(channel_idx = -1) {
    post_schedule_update(channel_idx);
    console.log("next update_fup_duration_element");
    // update select list
    duration = document.getElementById("fupdur_" + channel_idx).value;
    update_fup_duration_element(channel_idx, 0, (duration>0)); //oletuksilla katso parametrit
    update_fup_schedule_element(channel_idx);
    document.getElementById("fupbtn" + channel_idx).disabled = true;

    setTimeout(function () { updateStatus(false); }, 500);

}

function init_dashboard_section() {
    get_price_data(); // price data for manual scheduling / price hints
    init_channel_elements(false); //dashboard, no edit
}

function activate_section(section_id_full) {
    url_a = section_id_full.split("_");
    section_id = url_a[0];

    set_url_bar(section_id);
    active_section_id = section_id;

    // show new section div….
    let section_divs = document.querySelectorAll("div[id^='section_']");
    for (i = 0; i < section_divs.length; i++) {
        is_active_div = section_divs[i].id == "section_" + section_id;
        section_divs[i].style.display = is_active_div ? "block" : "none";
    }
    if (section_id == "channels" && url_a.length == 2) {
        var scroll_element = document.getElementById(url_a[1]);
        console.log("Try to scroll", section_id_full, url_a[1]);
        if (scroll_element)
            scroll_element.scrollIntoView();
    }
    //  else
    //      console.log("No scroll", section_id_full);
}


function init_services_section() {
    var variable_mode;
    // replicat mode disabled
    /*  if (document.getElementById("VARIABLE_SOURCE_ENABLED").value == 0) {
          variable_mode = 1;
          document.getElementById("variable_mode_0").disabled = true;
      }
      else {
          variable_mode = g_config.variable_mode; //document.getElementById("variable_mode_db").value;
      }*/
    // variable_mode = g_config.variable_mode; //should be 0 
    variable_mode = 0
    setVariableMode(variable_mode);
    document.getElementById("variable_mode_" + variable_mode).checked = true;

    document.getElementById("emt").value = g_config.energy_meter_type;
    setEnergyMeterFields(g_config.energy_meter_type);
    document.getElementById("emh").value = g_config.energy_meter_host;
    document.getElementById("emp").value = g_config.energy_meter_port;
    document.getElementById("emid").value = g_config.energy_meter_id;
    document.getElementById("emid").value = g_config.energy_meter_id;
    document.getElementById("baseload").value = g_config.baseload;
    document.getElementById("entsoe_api_key").value = g_config.entsoe_api_key;

    setEnergyMeterFields(g_config.energy_meter_type);

    //set forecast location select element
    var location = g_config.forecast_loc; //foredocument.getElementById("forecast_loc_db").value;
    $('#forecast_loc option').filter(function () {
        return this.value.indexOf(location) > -1;
    }).prop('selected', true);

    //set forecast location select element
    var area_code = g_config.entsoe_area_code; //document.getElementById("entsoe_area_code_db").value;
    if (!(area_code))
        area_code = "#";
    $('#entsoe_area_code option').filter(function () {
        return this.value.indexOf(area_code) > -1;
    }).prop('selected', true);

    // show influx definations only if enbaled by server
    if (g_constants.INFLUX_REPORT_ENABLED) {
        document.getElementById("influx_url").value = g_config.influx_url;
        document.getElementById("influx_token").value = g_config.influx_token;
        document.getElementById("influx_org").value = g_config.influx_org;
        document.getElementById("influx_bucket").value = g_config.influx_bucket;
    }
    else {
        document.getElementById("influx_div").style.display = "none";
    }
}
function init_channels_section() {
    init_channel_elements(true);
    getVariableList();
    initChannelForm();
    // update_discovered_devices(); //disabled, stability problems?
    //scroll to anchor, done after page is created
    url_a = window.location.href.split("#");
    if (url_a.length == 2) {
        var element = document.getElementById(url_a[1]);
        if (element)
            element.scrollIntoView();
    }
}

function init_admin_section() {
    initWifiForm();
    if (g_config.using_default_password) {
        document.getElementById("password_note").innerHTML = "Change your password - now using default password!"
    }
    document.getElementById("wifi_password").value = g_config.wifi_password;
    //set timezone select element 
    var timezone = g_config.timezone;// document.getElementById("timezone_db").value;
    $('#timezone option').filter(function () {
        return this.value.indexOf(timezone) > -1;
    }).prop('selected', true);

    // set language select element
    var lang = g_config.lang; //document.getElementById("lang_db").value;
    $('#lang option').filter(function () {
        return this.value.indexOf(lang) > -1;
    }).prop('selected', true);

    //set hw_template select list
    var hw_template_id = g_config.hw_template_id;// document.getElementById("hw_template_id_db").value;
    $('#hw_template_id option').filter(function () {
        return this.value.indexOf(hw_template_id) > -1;
    }).prop('selected', true);
}

// new experimental for one page ui
function init_sections() {
    setTimeout(function () { $("#cover").fadeOut(200) }, 10000); // uncover main screen, just in case if there is an error

    //constants generated from 
    $.ajax({
        url: '/data/ui-constants.json',
        dataType: 'json',
        async: false,
        success: function (data) { g_constants = data; console.log("got g_constants"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_constants", textStatus, jqXHR.status);
        }
    });


    //current config
    $.ajax({
        url: '/export-config',
        dataType: 'json',
        async: false,
        success: function (data) { g_config = data; console.log("got g_config"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_config", textStatus, jqXHR.status);
        }
    });

    init_dashboard_section();
    init_services_section();
    init_channels_section();
    init_admin_section();

    // activate requested section
    url_a = window.location.href.split("#");
    if (url_a.length == 2)
        activate_section(url_a[1]);
    else
        activate_section("dashboard");

    var footerdiv = document.getElementById("footerdiv");
    if (footerdiv) {
        footerdiv.innerHTML = "<br><div class='secbr'><a href='https://github.com/Netgalleria/arska-node/wiki' target='arskaw'>Arska Wiki</a> </div><div class='secbr'><i>Program version: " + g_constants.VERSION + " (" + g_constants.HWID + "),   Filesystem version: " + g_constants.version_fs + "</i></div>";
    }
    updateStatus(true);
    //show page when fully loaded
    $("#cover").fadeOut(200);//.hide(); 

}
//
function initForm(url) { // called from html onload
    setTimeout(function () { $("#cover").fadeOut(200) }, 10000); // uncover main screen, just in case if there is an error
    initUrlBar(url);

    //constants generated from 
    $.ajax({
        url: '/data/ui-constants.json',
        dataType: 'json',
        async: false,
        success: function (data) { g_constants = data; console.log("got g_constants"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_constants", textStatus, jqXHR.status);
        }
    });


    //current config
    $.ajax({
        url: '/export-config',
        dataType: 'json',
        async: false,
        success: function (data) { g_config = data; console.log("got g_config"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_config", textStatus, jqXHR.status);
        }
    });


    if (url == '/admin') {
        init_admin_section();
    }
    else if (url == '/channels') {
        init_channels_section();
    }
    else if (url == '/') {
        init_dashboard_section();
    }

    else if (url == '/inputs') {
        init_services_section();
    }

    var footerdiv = document.getElementById("footerdiv");
    if (footerdiv) {
        footerdiv.innerHTML = "<br><div class='secbr'><a href='https://github.com/Netgalleria/arska-node/wiki' target='arskaw'>Arska Wiki</a> </div><div class='secbr'><i>Program version: " + g_constants.VERSION + " (" + g_constants.HWID + "),   Filesystem version: " + g_constants.version_fs + "</i></div>";
    }
    updateStatus(true);
    //show page when fully loaded
    $("#cover").fadeOut(200);//.hide(); 
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
  
    return true;
}

function doAction(action) {

    actiond_fld = document.getElementById("action");
    if (actiond_fld)
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
            return false;
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
    else if (action == 'discover_devices') {
        // this version adds discovery task and after timeout queries results
        $.ajax({
            url: '/do?action=discover_devices',
            dataType: 'json',
            async: false,
            success: function (data) { console.log(data); }
        });
        setTimeout(function () { update_discovered_devices(); }, 5000);
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

