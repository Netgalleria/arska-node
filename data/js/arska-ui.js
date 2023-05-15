var g_config;
var g_constants;
var g_templates;

var day_ahead_chart_obj;

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
const CH_TYPE_GPIO_USR_INVERSED = 10;

const CH_TYPE_MODBUS_RTU = 20;
const CH_TYPE_DISABLED = 255;

const VARIABLE_PRODUCTION_POWER = "101";
const VARIABLE_SELLING_POWER = "102";
const VARIABLE_SELLING_ENERGY = "103";
const VARIABLE_PRODUCTION_ENERGY = "105";



let variable_list = {}; // populate later

window.onload = function () {
    init_ui();
}

let load_count = 0;
function populate_releases() {
    $.ajax({
        url: '/releases',
        dataType: 'json',
        async: false,
        success: function (data, textStatus, jqXHR) {
            load_count++;
            console.log('got releases');
            hw = data.hw;
            if (!(data.hasOwnProperty('releases'))) { /* retry to get releases*/
                if (load_count < 5)
                    setTimeout(function () { populate_releases(); }, 5000);
                // else
                //     document.getElementById('div_upd2').style.display = 'none';
            }
            else {
                releases_select_ctrl = document.getElementById("sel_releases");
                remove_select_options(releases_select_ctrl);
                addOption(releases_select_ctrl, "#", 'select version', true);
                $.each(data.releases, function (i, release) {
                    d = new Date(release[1] * 1000);
                    addOption(releases_select_ctrl, release[0], release[0] + ' ' + d.toLocaleDateString());
                });
                $('#releases\\:refresh').prop('disabled', true);
                $('#releases\\:update').prop('disabled', false);
                $('#sel_releases').prop('disabled', false);

                if (g_constants.VERSION_SHORT) {
                    version_base = g_constants.VERSION_SHORT.substring(0, g_constants.VERSION_SHORT.lastIndexOf('.'));
                    console.log('version_base', version_base);
                    $('#sel_releases option:contains(' + version_base + ')').append(' ***');
                    //  $('#sel_releases').val(version_base);
                }
                $('#sel_releases').val("#");

            }
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log('Cannot get releases', textStatus, jqXHR.status);
            document.getElementById('div_upd2').style.display = 'none';
        }
    });
};
function _(el) { return document.getElementById(el); }
function upload() { var file = _('firmware').files[0]; var formdata = new FormData(); formdata.append('firmware', file); var ajax = new XMLHttpRequest(); ajax.upload.addEventListener('progress', progressHandler, false); ajax.addEventListener('load', completeHandler, false); ajax.addEventListener('error', errorHandler, false); ajax.addEventListener('abort', abortHandler, false); ajax.open('POST', 'doUpdate'); ajax.send(formdata); }
function progressHandler(event) { _('loadedtotal').innerHTML = 'Uploaded ' + event.loaded + ' bytes of ' + event.total; var percent = (event.loaded / event.total) * 100; _('progressBar').value = Math.round(percent); _('status').innerHTML = Math.round(percent) + '&percnt; uploaded... please wait'; }
function reloadAdmin() { window.location.href = '/'; }
function completeHandler(event) { _('status').innerHTML = event.target.responseText; _('progressBar').value = 0; setTimeout(reloadAdmin, 20000); }
function errorHandler(event) { _('status').innerHTML = 'Upload Failed'; }
function abortHandler(event) { _('status').innerHTML = 'Upload Aborted'; }

function whatDecimalSeparator() {
    var n = 1.1;
    n = n.toLocaleString().substring(1, 2);
    return n;
}

const template_form_html = `<div class="modal fade"  id="ch_(ch#)_tmpl_form" aria-hidden="true" aria-labelledby="ch_(ch#)_tmpl_title" tabindex="-1">
<div class="modal-dialog modal-dialog-centered modal-dialog-scrollable modal-lg">
  <div class="modal-content">
    <div class="modal-header">
      <h5 class="card-title mb-0" id="ch_(ch#)_tmpl_title">Channel template parameters </h5>
      <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
    </div>
    <div id="ch_(ch#)_tmpl_mbody" class="modal-body">
        <p><strong><span id="ch_(ch#)_tmpl_name"></span></strong></p>
      <p><span id="ch_(ch#)_tmpl_desc"></span></p>
    </div>
    <div class="modal-footer">
        <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
      <button type="button" class="btn btn-primary btn-close"">OK</button>
    </div>
  </div>
</div>
</div>`;
/*
const template_fld_html = `<label for="ch_(ch#)_templ_field_(fld#)" class="form-label">Max daytime price up</label>
<br>
<span class="text-muted">Max price to keep the channe up daytime (e.g. 15c/kWh)?</span>
<input id="ch_(ch#)_ch_0:templ_field_(fld#)" type="number" class="form-control"  placeholder="15">
<br>`;
*/

const schedule_html = `<div class="col"><div class="card white-card" id="sch_(ch#):card">
                  <div class="card-header d-flex align-items-center justify-content-between">
                    <h5 class="card-title m-lg-0 p-1 ">
                        <span id="sch_(ch#):label" class="channel-label"></span>
                        <span id="sch_(ch#):title"></span>
                    </h5>
                        
                    <label id="sch_(ch#):status" class="btn btn-secondary btn-sm text-bg-success">
                      <span id="sch_(ch#):status_icon" data-feather="zap" class="align-text-bottom"></span>
                      <span id="sch_(ch#):status_txt"></span>
                    </label>
                  </div>
                  <!--card-header-->
                  <div class="card-body">
                  <span class="text-muted">Current schedule</span>
                    <div class="input-group mb-0">
                      <span class="input-group-text bg-light" >
                        <span data-feather="calendar" class="align-text-bottom"></span>
                      </span>
                      <span class="input-group-text bg-light col-lg-3" id="sch_(ch#):duration_c">-</span>
                      <span class="input-group-text bg-light col-lg-6" id="sch_(ch#):start_c">-</span>
                      <input id="sch_(ch#):delete" type="radio" class="btn-check p-0" value="0">
                      <label class="btn btn-secondary" for="sch_(ch#):delete">
                        <span data-feather="delete" class="align-text-bottom" style="pointer-events: none;"></span>
                        </label>
                    </div>
                    <span class="text-muted">Update schedule</span>
                    <!--./input-group-->
                    <div class="input-group mb-3">
                      <span class="input-group-text bg-light" >
                        <span data-feather="edit-3" class="align-text-bottom" style="pointer-events: none;"></span>
                      </span>
                      <div class="col-md-6 col-lg-3">
                        <select id="sch_(ch#):duration" class="form-select" aria-label="variable" data-bs-toggle="tooltip" title="Duration of the schedule hh:mm">
                        </select>
                      </div>
                      <!--./col-->
                      <div class="col-md-6 col-lg-6">
                        <select id="sch_(ch#):start" class="form-select" aria-label="variable">
                        </select>
                      </div>
                      <!--./col-->
                      <span class="input-group-text p-0" > </span>
                      <input id="sch_(ch#):save" type="radio" class="btn-check" name="sch_(ch#):config_mode" 
                        value="0">
                      <label class="btn btn-secondary" for="sch_(ch#):save">
                        <span data-feather="plus" class="align-text-bottom" style="pointer-events: none;"></span>
                        </label>
                    </div>
                    <!--./input-group-->
                    <div id="sch_(ch#):alert">
                    </div>
                  </div>
                  <!--./card-body-->
                </div></div>`;


const channel_html = `<div class="col">
    <!-- Channel (ch#)
================================================== -->
    <!-- channel starts -->
    <div class="card white-card mb-3" id="ch_(ch#):card">
        <div class="card-header">
            <h5 id="ch_(ch#):title" class="card-title m-lg-0 p-1"><span
                    class="channel-label channel-label-0"></span></h5>
        </div>
        <div class="card-body pt-2">
            <!--basic settings-->
            <div class="form-section">
                <h6 class="form-section-title">Basic Settings</h6>
                <div class="row g-3">
                    <div class="col-12 col-md-6">
                        <label for="ch_(ch#):id_str" class="form-label">Channel id</label>
                        <input id="ch_(ch#):id_str" type="text" class="form-control"
                            placeholder="Channel name" maxlength="20">
                    </div>
                    <!--./col-->
                    <div class="col-6 col-lg-auto">
                        <label for="ch_(ch#):channel_color" class="form-label">Channel
                            color</label>
                        <input type="color" class="form-control form-control-color"
                            id="ch_(ch#):channel_color" value="#005d85" data-bs-toggle="tooltip"
                            data-bs-original-title="Select custom Channel color.">
                    </div>
                    <!--./col-->
                </div>
                <!--./row-->
            </div>
            <!--./form-section-->
            <!--Relay settings-->
            <div class="form-section">
                <h6 class="form-section-title">Relay Settings</h6>
                <div class="row g-3">
                    <div class="col-md-3">
                        <label for="ch_(ch#):type" class="form-label">Relay type:</label>
                        <select id="ch_(ch#):type" class="form-select" aria-label="variable">
                        </select>
                    </div>
                    <!--./col-->
                    <div id="ch_(ch#):r_id_div" class="col-md-3">
                        <label for="ch_(ch#):r_id" class="form-label">GPIO:</label>
                        <input id="ch_(ch#):r_id" type="number" class="form-control" disabled=""  placeholder="33" min="0" step="1" max="33">
                       
                    </div>
                    <!--./col-->
                    <div id="ch_(ch#):r_ip_div" class="col-md-4">
                        <label for="ch_(ch#):r_ip" class="form-label">IP Address:</label>
                        <input id="ch_(ch#):r_ip" type="text" class="form-control"
                            placeholder="0.0.0.0" required=""
                            pattern="((^|.)((25[0-5])|(2[0-4]d)|(1dd)|([1-9]?d))){4}$">
                    </div>
                    <!--./col-->
                    <div id="ch_(ch#):r_uid_div" class="col-md-1">
                        <label for="ch_(ch#):r_id" class="form-label">ID:</label>
                        <input id="ch_(ch#):r_uid" type="number" class="form-control" placeholder="0" min="0" step="1" max="255">
                    </div>
                    <!--./col-->
                </div>
                <!--./row-->
            </div>
            <!--./form-section-->
            <!--Channel rules-->

            <div class="accordion" id="ch_(ch#)_accordion">
                <div class="accordion-item">
                    <h2 class="accordion-header" id="ch_(ch#)_accordionh3">
                        <button class="accordion-button"
                            type="button" data-bs-toggle="collapse"
                            data-bs-target="#ch_(ch#)_colla_rules" aria-expanded="true"
                            aria-controls="ch_(ch#)_colla_rules">
                            Channel Control Rules
                        </button>
                    </h2>
                    
                    <div id="ch_(ch#)_colla_rules" class="accordion-collapse collapse collapsed"
                        aria-labelledby="ch_(ch#)_accordionh3" data-bs-parent="#ch_(ch#)_accordion">
                        <div class="accordion-body">
                            <div class="row g-3 mb-3">
                                <div class="col-auto">
                                    <label for="ch_(ch#):uptime_minimum_m" class="form-label">Minimum uptime
                                    (minutes)</label>
                                    <input id="ch_(ch#):uptime_minimum_m" type="number" class="form-control" placeholder="5" min="0" step="1" max="480" >
                                </div>
                            <!--./col-->
                            <div class="col-auto">
                                <label for="ch_(ch#):priority" class="form-label">Priority</label>
                                <input id="ch_(ch#):priority" type="number" class="form-control" placeholder="0" min="0" step="1" max="255" >
                            </div>
                            <!--./col--> 
                            <div class="col-auto">
                                <label for="ch_(ch#):load" class="form-label">Load (W)</label>
                                <input id="ch_(ch#):load" type="number" class="form-control" placeholder="0" min="0" step="500" max="50000" >
                            </div>
                            <!--./col-->
                      
                            </div>
                            <div class="row mb-3 g-3">
                                <div class="col">
                                    <div class="input-group mb-3">
                                        <span class="input-group-text"
                                            id="ch_(ch#):basic-addon3">Config mode</span>
                                        <input id="ch_(ch#):config_mode_1" type="radio"
                                            class="btn-check" name="ch_(ch#):config_mode"
                                             checked="" value="1">
                                        <label class="btn btn-secondary"
                                            for="ch_(ch#):config_mode_1">
                                            <span data-feather="file"
                                            class="align-text-bottom" style="pointer-events: none;"></span>Template</label>

                                        <input id="ch_(ch#):config_mode_0" type="radio"
                                            class="btn-check" name="ch_(ch#):config_mode"
                                             value="0">
                                        <label class="btn btn-secondary"
                                            for="ch_(ch#):config_mode_0">
                                            <span data-feather="list" class="align-text-bottom" style="pointer-events: none;"></span>
                                            Advanced</label>
                                    </div>
                                    <div class="input-group mb-3">
                                        <select id="ch_(ch#):template_id" class="form-select"
                                            aria-label="variable" disabled="">
                                            <option value="-1">Select template</option>
                                        </select>
                                        <span class="input-group-text p-0"> </span>
                                        <button id="ch_(ch#):template_reset" type="button"
                                            class="btn btn-primary" disabled="" >
                                            <span data-feather="settings"
            class="align-text-bottom" style="pointer-events: none;"></span>
                                        </button>
                                    </div>

                                    <div class="mb-3"><span id="ch_(ch#):desc" class="text-muted mb-3"></span></div>

                                    <div class="modal fade"  id="ch_(ch#)_tmpl_form" aria-hidden="true" aria-labelledby="ch_(ch#)_tmpl_title" tabindex="-1">
                                    <div class="modal-dialog modal-dialog-centered modal-dialog-scrollable">
                                      <div class="modal-content">
                                        <div class="modal-header">
                                          <h5 class="card-title mb-0" id="ch_(ch#)_tmpl_title">Channel template parameters </h5>
                                          <button type="button" class="btn-close" data-bs-dismiss="modal" aria-label="Close"></button>
                                        </div>
                                        <div id="ch_(ch#)_tmpl_mbody" class="modal-body">
                                            <p><strong><span id="ch_(ch#)_tmpl_name">Template Name</span></strong></p>
                                          <p><span id="ch_(ch#)_tmpl_desc">Template Name template description</span></p>

                                          <div id="ch_(ch#)_tmpl_fields">
                                          </div>
                                        </div>
                        
                                        <div class="modal-footer">
                                            <button type="button" class="btn btn-secondary" data-bs-dismiss="modal">Cancel</button>
                                          <button id="ch_(ch#)_tmpl_close" type="button" class="btn btn-primary">OK</button>
                                        </div>
                                      </div>
                                    </div>
                                    </div>
                                   <!-- <a class="mt-3 d-block" data-bs-target="#ch_(ch#)_tmpl_form" data-bs-toggle="modal">Example channel rules - modal example here: https://getbootstrap.com/docs/5.2/components/modal/#varying-modal-content </a> 
                                   -->
                                </div>
                                <!--./col-->
                            </div>
                            <!--./row-->
                            <div id="ch_(ch#):rules"
                                class="row row row-cols-1 row-cols-md-2 g-3">      
                                <!--./col-->
                            </div>
                            <!--./row-->
                        </div>

                    </div>
                </div> <!-- accordion -->
            </div>
            <div id="ch_(ch#):alert"></div>
        </div>
        <!--./card-body-->
        <div class="card-footer text-end">
            <button id="ch_(ch#):save"  class="btn btn-primary" type="submit" disabled="" >
            <span data-feather="save" style="pointer-events: none;"  class="align-text-bottom"></span>
                Save</button>
        </div>
        <!--./card-footer-->
    </div> <!-- channel ends-->
</div> <!-- col ends -->`;

const rule_html = `<div class="col">
     <!-- rule starts -->
     <div id="ch_#:r_#:card" class="card rule-card">
         <div class="card-header">
             <h5 id="ch_#:r_#:title" class="card-title">Rule 1</h5>
             <div class="mt-3" style="margin-left: 0.5rem;"><span id="ch_#:r_#:desc"class="text-muted"></span></div>
         </div>
         <div class="card-body">
             <div class="input-group mb-3">
                 <span class="input-group-text">Matching rule sets
                     channel</span>
                 <input id="ch_#:r_#:up_0" type="radio"
                     class="btn-check" name="ch_#:r_#:up" checked="">
                 <label class="btn btn-secondary"
                     for="ch_#:r_#:up_0">
                     <span data-feather="zap-off"
            class="align-text-bottom" style="pointer-events: none;"></span>
                     down</label>
                 <input id="ch_#:r_#:up_1" type="radio"
                     class="btn-check" name="ch_#:r_#:up">
                 <label class="btn btn-secondary"
                     for="ch_#:r_#:up_1">
                     <span data-feather="zap"
            class="align-text-bottom" style="pointer-events: none;"></span>
                     up</label>
             </div>
             
             <span class="row g-1 form-label">Conditions (and):</span>
             <div class="rule-container">
                 <div id="ch_#:r_#:stmts"> 
                 </div>
             </div> <!-- container -->
            

         </div> <!-- card body -->
     </div> <!-- rule ends -->
 </div>`;


const stmt_html = `<div class="row g-1">
<div class="col-6">
    <select id="ch_#:r_#:s_#:var"
        class="form-select"
        aria-label="variable">
    </select>
</div>
<div class="col-3">
    <select id="ch_#:r_#:s_#:oper"
        class="form-select visible"
        aria-label="oper">
        <option value="-1">&nbsp;</option>

    </select>
</div>
<div class="col col-3">
    <input id="ch_#:r_#:s_#:const"
        type="number"
        class="form-control visible"
        placeholder="value"
        aria-label="value">
</div>
</div>`;

//localised text
function _ltext(obj, prop) {
    if (obj.hasOwnProperty(prop + '_' + g_config.lang))
        return obj[prop + '_' + g_config.lang];
    else if (obj.hasOwnProperty(prop))
        return obj[prop];
    else
        return '[' + prop + ']';
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

let last_chart_update = 0;

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

// graph
//var prices = [];
var price_data = null;
var net_exports = [];

let has_history_values = {};
var variable_history;
var channel_history = [];

var prices_first_ts = 0;
var prices_last_ts = 0;
var prices_resolution_min = 60;
var prices_expires = 0;

// update variables and channels statuses to channels form
function update_status(repeat) {
    console.log("update_status starting");
    if (!(typeof Chart === 'function')) { //wait for chartJs script loading
        setTimeout(function () { update_status(repeat); }, 2000);
        return;
    }

    //moratorium, do not query status from the controller during busiest processing times
    /*const now_date = new Date();
    if ((now_date.getSeconds() < 10) || (now_date.getMinutes() == 0 && now_date.getSeconds() < 30)) {
        setTimeout(function () { update_status(repeat); }, 5000);
        return;
    } */

    show_variables = true;
    //TODO: add variable output

    const interval_s = 60;
    const process_time_s = 15;
    let next_query_in = interval_s;


    var jqxhr_obj = $.ajax({
        url: '/status',
        cache: false,
        dataType: 'json',
        async: false,
        success: function (data, textStatus, jqXHR) {
            console.log("got status data", textStatus, jqXHR.status);
            // moved from chart creation create_dashboard_chart
            channel_history = data.channel_history;
            variable_history = data.variable_history;
            for (const variable_code in data.variable_history) {
                for (i = 0; i < data.variable_history[variable_code].length; i++) {
                    if (Math.abs(data.variable_history[variable_code][i]) > 1) {
                        has_history_values[variable_code] = true;
                        break;
                    }
                }
            }
            //** 

            msgdiv = document.getElementById("dashboard:alert");
            keyfd = document.getElementById("keyfd");

            if (data.hasOwnProperty('next_process_in'))
                next_query_in = data.next_process_in + process_time_s + Math.floor(Math.random() * 10);

            if (g_config.wifi_in_setup_mode)
                extra_message = " <a class='chlink' onclick='jump(\"admin:network\");'>Configure Wi-Fi settings.</a>";
            else if (g_config.using_default_password) {
                extra_message = " <a class='chlink' onclick='jump(\"admin:network\");'>Change</a> your admin password - now using default password!";
            }
            else {
                extra_message = "";
            }

            if (msgdiv && (data.last_msg_ts > getCookie("msg_read"))) {
                if (data.last_msg_type == 1)
                    msg_type = 'info';
                else if (data.last_msg_type == 2)
                    msg_type = 'warning';
                else if (data.last_msg_type == 3)
                    msg_type = 'error';
                else
                    msg_type = 'success';

                msgDateStr = get_time_string_from_ts(data.last_msg_ts, true, true);

                live_alert("dashboard", msgDateStr + ' ' + data.last_msg_msg + extra_message, msg_type);

                $('#dashboard\\:alert_i').on('closed.bs.alert', function () {
                    setCookie("msg_read", Math.floor((new Date()).getTime() / 1000), 10);
                    //   document.getElementById("msgdiv").innerHTML = "";
                })
            }

            if (document.getElementById("started_str"))
                document.getElementById("started_str").innerHTML = ts_date_time(data.started);

            selling = isNaN(data.variables[VARIABLE_SELLING_ENERGY]) ? "-" : data.variables[VARIABLE_SELLING_ENERGY] + " Wh";
            document.getElementById("db:export_v").innerHTML = selling;

            now_period_start = Math.max(data.started, parseInt(data.ts / 3600) * 3600);
            // em_period_s_date = new Date(now_period_start * 1000);
            //  em_period_e_date = new Date(data.ts * 1000);

            ///localtime
            //document.getElementById("db:current_period").innerHTML = "(" + em_period_s_date.toLocaleTimeString('fi-FI', { hour12: false }).substring(0, 5) + "-" + em_period_e_date.toLocaleTimeString('fi-FI', { hour12: false }).substring(0, 5) + ")";
            document.getElementById("db:current_period").innerHTML = "(" + get_time_string_from_ts(now_period_start, false) + "-" + get_time_string_from_ts(data.ts, false) + ")";
            document.getElementById("db:production_d").style.display = isNaN(data.variables[VARIABLE_PRODUCTION_ENERGY]) ? "none" : "block";

            if (!isNaN(data.variables[VARIABLE_PRODUCTION_POWER])) {
                document.getElementById("db:production_v").innerHTML = data.variables[VARIABLE_PRODUCTION_ENERGY] + " Wh";;
            }
            price = isNaN(data.variables["0"]) ? '-' : data.variables["0"] + ' ¢/kWh ';

            document.getElementById("db:price_v").innerHTML = price;

            //sensor values
            for (s_idx = 201; s_idx <= 203; s_idx++) {
                sensor_has_value = !isNaN(data.variables[s_idx.toString()]);
                if (sensor_has_value) {
                    document.getElementById(`db:s_${s_idx}:val`).innerHTML = data.variables[s_idx.toString()] + "&deg;C";
                }
                document.getElementById(`db:s_${s_idx}:div`).style.display = sensor_has_value ? "block" : "none";
            }
            now_ts = Date.now() / 1000;
            now_period_start = parseInt(now_ts / 3600) * 3000;
            // TODO: update

            if (show_variables) {
                //   document.getElementById("variables").style.display = document.getElementById("statusauto").checked ? "block" : "none";
                $("#tblVariables_tb").empty();
                $.each(data.variables, function (id, variable) {

                    var_this = getVariable(id);
                    variable_name = "";
                    variable_desc = "";
                    if (var_this) {
                        if (var_this[0] in variable_list) {
                            // variable_desc = variable_list[var_this[0]]["en"]; //TODO: multilang
                            variable_desc = _ltext(variable_list[var_this[0]], "desc");
                        }
                        if (Array.isArray(variable)) {
                            var_value = JSON.stringify(variable);
                        }
                        else {
                            var_value = variable.replace('"', '').replace('"', '');
                        }
                        newRow = '<tr><th scope="row">' + var_this[0] + '</th><td>' + var_this[1] + '</td><td>' + var_value + '</td><td>' + variable_desc + '</td></tr>';
                        $(newRow).appendTo($("#tblVariables_tb"));
                    }

                });

                $('#vars_updated').text('updated: ' + ts_date_time(data.ts, include_year = true));
            }
            //console.log("Starting populate_channel_status loop");
            $.each(data.ch, function (i, ch) {
                populate_channel_status(i, ch)
            });


            if (Math.floor(now_ts / 600) != Math.floor(last_chart_update / 600)) {
                console.log("Interval changed in update_status");
                //  update_schedule_select_periodical(); //TODO:once after hour/period change should be enough
                price_chart_ok = create_dashboard_chart();
            }
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

    if (repeat) {
        setTimeout(function () { update_status(true); }, next_query_in * 1000);
    }
}


function populate_channel_status(channel_idx, ch) {
    //TODO: update this to new...
    now_ts = Date.now() / 1000;
    // console.log(channel_idx,ch);
    sch_duration_c_span = document.getElementById(`sch_${channel_idx}:duration_c`);

    sch_start_c_span = document.getElementById(`sch_${channel_idx}:start_c`);
    sch_delete_radio = document.getElementById(`sch_${channel_idx}:delete`);
    sch_status_label = document.getElementById(`sch_${channel_idx}:status`);
    sch_status_icon_span = document.getElementById(`sch_${channel_idx}:status_icon`);
    sch_status_text_span = document.getElementById(`sch_${channel_idx}:status_txt`);

    if ((ch.force_up_until > now_ts)) {
        //same duration as scheduled
        document.getElementById(`sch_${channel_idx}:duration`).value = parseInt((ch.force_up_until - ch.force_up_from) / 60); //same default duration for input/update
        update_fup_schedule_element(channel_idx);

        duration_c_m = parseInt((ch.force_up_until - ch.force_up_from) / 60);
        duration_c_str = pad_to_2digits(parseInt(duration_c_m / 60)) + ":" + pad_to_2digits(duration_c_m % 60);
        sch_duration_c_span.innerHTML = duration_c_str;

        sch_start_c_span.innerHTML = get_time_string_from_ts(ch.force_up_from, false, true) + " &rarr; ";// + get_time_string_from_ts(ch.force_up_until, false, true);
        //    console.log("sch_start_c_span.innerText", sch_start_c_span.innerText);
    }
    else {
        sch_duration_c_span.innerHTML = "-";
        sch_start_c_span.innerHTML = "-";
    }
    sch_delete_radio.disabled = (ch.force_up_until <= now_ts);

    sch_status_label.classList.remove(ch.is_up ? "text-bg-danger" : "text-bg-success");
    sch_status_label.classList.add(ch.is_up ? "text-bg-success" : "text-bg-danger");

    info_text = "";
    if (ch.active_condition > -1)
        // rule_link_a = " onclick='activate_section(\"channels_c" + channel_idx + "r" + ch.active_condition + "\");'";
        rule_link_a = "";
    rule_link_a = " onclick='jump(\"channels:ch_" + channel_idx + ":r_" + ch.active_condition + "\");'";

    if (g_config.ch[channel_idx]["type"] == 0) {
        info_text = "Relay undefined";
        sch_status_label.classList.add("text-bg-muted");
        sch_status_label.classList.remove("text-bg-danger");
        sch_status_label.classList.remove("text-bg-success");
    }

    else if (ch.is_up) {
        if ((ch.force_up_from <= now_ts) && (now_ts < ch.force_up_until))
            info_text += "Up based on manual schedule.";
        else if (!ch.wanna_be_up)
            info_text += "Up, but going down.";
        else if (ch.active_condition > -1)
            info_text += "Up based on <a class='chlink' " + rule_link_a + ">rule " + (ch.active_condition + 1) + "</a>. ";

    }
    else {
        if (ch.wanna_be_up)
            info_text += "Down, but going up.";
        else if ((ch.active_condition > -1))
            info_text += "Down based on <a class='chlink' " + rule_link_a + ">rule " + (ch.active_condition + 1) + "</a>. ";
        else if ((ch.active_condition == -1))
            info_text += "Down, no matching rules. ";

    }

    sch_status_text_span.innerHTML = info_text;
    return;
}



function link_to_wiki(article_name) {
    return '<a class="helpUrl" target="wiki" href="https://github.com/Netgalleria/arska-node/wiki/' + article_name + '">ℹ</a>';
}




function sleep(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
}

function ts_date_time(ts, include_year = true) {
    date = new Date(ts * 1000);
    date_str = pad_to_2digits(date.getDate()) + '.' + pad_to_2digits(date.getMonth() + 1) + '.';
    if (include_year)
        date_str += date.getFullYear();
    date_str += ' ' + pad_to_2digits(date.getHours()) + ':' + pad_to_2digits(date.getMinutes())

    return date_str;
}

let price_chart_dataset = [];

function create_dashboard_chart() {
    let date;
    let time_labels = [];
    let prices_out = [];
    let idx = 0;
    let now_idx = 0;
    let now_idx_chh = 23;
    now_ts = (Date.now() / 1000);
    now_period_ts = parseInt(now_ts / 3600) * 3600;

    let price_data_exists = (price_data !== null);
    if (!price_data_exists) { //try to get prices now 
        get_price_data(false);
    }

    if (price_data == null) {
        console.log("create_dashboard_chart: no price data");
        price_data_exists = false;
        // 48 periods
        chart_start_ts = parseInt(now_ts / 86400) * 86400 + (3600);
        chart_end_excl_ts = chart_start_ts + (48 * 3600);
        chart_resolution_m = 60;
        now_idx = 0;
        now_idx_chh = 23; //oli 23   
    }
    else {
        price_data_exists = true;
        chart_start_ts = price_data.record_start;
        chart_end_excl_ts = price_data.record_end_excl;
        chart_resolution_m = price_data.resolution_m;
    }
    const ctx = document.getElementById('day_ahead_chart').getContext('2d');

    var tz_offset = new Date().getTimezoneOffset();
    start_date_str = ts_date_time(chart_start_ts, false) + ' - ' + ts_date_time(chart_end_excl_ts - 3600, false);

    // now history, if values exists
    var chartExist = Chart.getChart("day_ahead_chart"); // <canvas> id
    if (chartExist != undefined)
        chartExist.destroy();

    for (ts = chart_start_ts; ts < chart_end_excl_ts; ts += (chart_resolution_m * 60)) {
        if (ts > now_ts && now_idx == 0)
            now_idx = idx - 1;


        time_labels.push(get_time_string_from_ts(ts, false, true));

        if (price_data_exists) {
            //   prices_out.push(Math.round(price_data.prices[idx] / 100) / 10);
            //    if (ts<now_ts+25*3600 && series_started)
            prices_out.push({ x: get_time_string_from_ts(ts, false, true), y: Math.round(price_data.prices[idx] / 100) / 10 });
        }
        idx++;
    }

    if (price_data_exists) {
        datasets = [{
            label: 'price ¢/kWh',
            data: prices_out,
            yAxisID: 'yp',
            borderColor: ['#2376DD'
            ],
            backgroundColor: '#2376DD',
            pointStyle: 'none',
            pointRadius: 0,
            pointHoverRadius: 5,
            fill: false,
            stepped: true,
            borderWidth: 2
        }
        ];
    }
    else
        datasets = [];

    console.log("now_idx", now_idx);

    var new_ds = [];
    console.log("has_history_values", has_history_values);

    // iterate

    if (has_history_values[VARIABLE_SELLING_ENERGY]) {
        //    console.log("VARIABLE_SELLING_ENERGY");
        dataset_started = false;
        for (h_idx = variable_history[VARIABLE_SELLING_ENERGY].length - 1 - now_idx; h_idx < variable_history[VARIABLE_SELLING_ENERGY].length; h_idx++) {
            if (Math.abs(variable_history[VARIABLE_SELLING_ENERGY][h_idx]) > 1)
                dataset_started = true;
            new_ds.push(dataset_started ? -variable_history[VARIABLE_SELLING_ENERGY][h_idx] : null);
        }
        // console.log("new_ds ", VARIABLE_SELLING_ENERGY, new_ds);
        if (dataset_started)
            datasets.push(
                {
                    label: 'import Wh',
                    data: new_ds,
                    yAxisID: 'y_energy',
                    cubicInterpolationMode: 'monotone',
                    borderColor: ['#0eb03c'
                    ],
                    backgroundColor: '#0eb03c',
                    pointStyle: 'circle',
                    pointRadius: 1,
                    pointHoverRadius: 5,
                    fill: false,
                    stepped: false,
                    borderWidth: 2
                });
    }

    //experimental solar forecast, could be combinet with get_price_data
    if (true) {

        $.ajax({
            url: '/series?solar_fcst=true',
            cache: false,
            dataType: 'json',
            async: false,
            success: function (data, textStatus, jqXHR) {
                console.log('got solar forecast', textStatus, jqXHR.status);
                let fcst_ds = [];
                if (!data.hasOwnProperty("solar_forecast"))
                    return false;

                solar_fcst = data.solar_forecast.s;
                start = data.solar_forecast.start;
                resolution_sec = data.solar_forecast.resolution_sec;
                series_started = false;
                for (idx = 0; idx < solar_fcst.length; idx++) {
                    ts = (start + idx * 3600);
                    if (solar_fcst[idx] > 0)
                        series_started = true;
                    if (series_started & ts < chart_end_excl_ts && data.solar_forecast.first_set_period <= ts && ts <= data.solar_forecast.last_set_period)
                        fcst_ds.push({ x: get_time_string_from_ts(ts, false, true), y: solar_fcst[idx] });
                }
                console.log("fcst_ds", fcst_ds);
                datasets.push(
                    {
                        label: 'solar fcst Wh',
                        data: fcst_ds,
                        yAxisID: 'y_energy',
                        cubicInterpolationMode: 'monotone',
                        borderColor: ['#ffff00'
                        ],
                        backgroundColor: '#ffff00',
                        pointStyle: 'circle',
                        pointRadius: 1,
                        pointHoverRadius: 5,
                        fill: false,
                        stepped: false,
                        borderWidth: 2
                    });

            },
            error: function (jqXHR, textStatus, errorThrown) {
                console.log("Cannot get v", textStatus, jqXHR.status);
            }
        });

    }

    if (has_history_values[VARIABLE_PRODUCTION_ENERGY]) {
        console.log("VARIABLE_PRODUCTION_ENERGY");
        dataset_started = false;
        let new_ds = [];
        for (h_idx = variable_history[VARIABLE_PRODUCTION_ENERGY].length - 1 - now_idx; h_idx < variable_history[VARIABLE_PRODUCTION_ENERGY].length; h_idx++) {
            if (Math.abs(variable_history[VARIABLE_PRODUCTION_ENERGY][h_idx]) > 1)
                dataset_started = true;
            new_ds.push(dataset_started ? variable_history[VARIABLE_PRODUCTION_ENERGY][h_idx] : null);
        }
        console.log("new_ds ", VARIABLE_PRODUCTION_ENERGY, new_ds);
        if (dataset_started)
            datasets.push(
                {
                    label: 'production Wh',
                    data: new_ds,
                    yAxisID: 'y_energy',
                    cubicInterpolationMode: 'monotone',
                    borderColor: ['#f58d42'
                    ],
                    backgroundColor: '#f58d42',
                    pointStyle: 'circle',
                    pointRadius: 1,
                    pointHoverRadius: 5,
                    fill: false,
                    stepped: false,
                    borderWidth: 2
                });
    }

    var channel_dataset;
    now_period_start = parseInt(now_ts / 3600) * 3600;
    first_chh_period = now_period_start - 23 * 3600;
    console.log("now_idx", now_idx, "now_idx_chh", now_idx_chh);
    for (channel_idx = 0; channel_idx < channel_history.length; channel_idx++) {
        if (g_config.ch[channel_idx]["type"] == 0) // undefined
            continue;
        channel_dataset = [];
        dataset_started = false;

        console.log("chh_idx [x - x [", now_idx_chh - now_idx, 35 + now_idx_chh - now_idx);
        for (h_idx = 0; h_idx < 36; h_idx++) {
            chh_idx = h_idx + now_idx_chh - now_idx;
            //  console.log("chh_idx",chh_idx);
            if (chh_idx < 0)
                channel_dataset.push(null);
            else if (chh_idx > 23) //?
                break;
            else {
                if (Math.abs(channel_history[channel_idx][chh_idx]) > 1)
                    dataset_started = true;
                channel_dataset.push(dataset_started ? channel_history[channel_idx][chh_idx] : null);
                //    console.log("pushed",dataset_started ? channel_history[channel_idx][chh_idx] : null);
            }
        }

        // console.log("channel_history", channel_idx, channel_history[channel_idx]);
        // console.log("channel_dataset", channel_dataset);
        datasets.push({
            label: g_config.ch[channel_idx]["id_str"],
            hidden: true,
            type: 'bar',
            parsing: true,
            data: channel_dataset,
            yAxisID: 'ych',
            borderColor: [g_config.ch[channel_idx]["channel_color"]
            ],
            backgroundColor: g_config.ch[channel_idx]["channel_color"],
            pointHoverRadius: 5,
            borderWidth: 1
        });
    }

    day_ahead_chart_obj = new Chart(ctx, {
        type: 'line',
        data: {
            labels: time_labels,
            datasets: datasets
        },
        options: {
            responsive: true,
            scales: {
                ynow: {
                    beginAtZero: true,
                    ticks: {
                        display: false
                    },
                    position: { x: now_idx + 0.5 }, grid: {
                        display: false,
                        lineWidth: 6, color: "#fabe0a", borderWidth: 2, borderColor: '#fabe0a'
                    }
                },
                yp: {
                    beginAtZero: true,
                    ticks: {
                        color: 'black',
                        font: { size: 12 },
                        callback: function (value, index, values) {
                            return value + ' ¢/kWh';
                        }
                    },
                    position: 'right', grid: {
                        lineWidth: 1, color: "#47470e", borderWidth: 1, borderColor: '#47470e'
                    }
                },
                y_energy: {
                    display: 'auto',
                    beginAtZero: true,
                    grid: { display: false },
                    ticks: {
                        color: '#4f4f42',
                        font: { size: 12 },
                        callback: function (value, index, values) {
                            return value + ' Wh';
                        }
                    }
                },
                ych: {
                    display: 'auto',
                    beginAtZero: true,
                    min: 0,
                    max: 60,
                    grid: { display: false },
                    ticks: {
                        color: '#4f4f42',
                        font: { size: 12 },
                        callback: function (value, index, values) {
                            return value + ' m';
                        }
                    }
                },
                x: { grid: { lineWidth: 0.5, display: true, color: "#47470e" } }
            },
            interaction: {
                intersect: false,
                axis: 'x'
            },

            plugins: {
                title: {
                    display: false,
                    text: "Day-ahead prices imported energy",
                    padding: 5,
                    font: {
                        size: 24,
                        family: ['Noto Sans', 'Helvetica Neue'],
                        weight: "normal"
                    }
                },
                subtitle: {
                    display: false,
                    text: 'Chart Subtitle',
                    color: 'blue',
                    font: {
                        size: 12,
                        family: '"Helvetica Neue","Noto Sans"',
                        weight: 'normal',
                        style: 'italic'
                    }
                },
                legend: {
                    display: true,
                    labels: { boxWidth: 15, boxHeight: 15 },
                    position: 'bottom',
                    font: {
                        family: "'lato', 'sans-serif'",
                        size: 12,
                        weight: "normal"
                    }
                }
            }
        }
    });

    Chart.defaults.color = '#0a0a03';
    Chart.defaults.scales.borderColor = '#262623';
    document.getElementById("chart_container").style.display = "block";

    last_chart_update = Date.now() / 1000;

    document.getElementById("dashboard:refresh").classList.remove('d-none');
    document.getElementById("dashboard:refresh").style.display = "block";

    return true;
}


function get_price_data(repeat = true) {
    now_ts = Date.now() / 1000;
    if (prices_expires > now_ts) { // not yet expired
        expires_in = (now_ts - prices_expires);
        console.log("Next get_price_data in" + expires_in + "seconds.");
        setTimeout(function () { get_price_data(); }, (1800 * 1000));
        return;
    }
    console.log("get_price_data starting");

    $.ajax({
        url: '/prices',
        cache: false,
        dataType: 'json',
        async: false,
        success: function (data, textStatus, jqXHR) {
            if (data.expires > now_ts + (-3600 * 4)) {
                console.log('got /prices', textStatus, jqXHR.status);
                price_data = data; //TODO: remove redundancy in variables
                prices_first_ts = data.record_start;
                prices_resolution_min = data.resolution_m;
                prices_last_ts = prices_first_ts + (price_data.prices.length - 1) * (prices_resolution_min * 60);
                prices_expires = data.expires;
                if (repeat)
                    setTimeout(function () { get_price_data(); }, 1800000);
            }
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get prices", textStatus, jqXHR.status);
            setTimeout(function () { get_price_data(); }, 40000);
        }
    });

}

function get_price_for_segment(start_ts, end_ts = 0) {
    if (price_data === null || price_data.prices === null || price_data.prices.length == 0) { //prices (not yet) populated
        console.log("get_price_for_segment, prices not populated              ");
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
        price_sum += price_data.prices[price_idx];
        price_count++;
    }
    var segment_price_avg = (price_sum / price_count) / 1000;
    return segment_price_avg;
}

function pad_to_2digits(num) {
    return num.toString().padStart(2, '0');
}

function get_time_string_from_ts(ts, show_secs = true, show_day_diff = false) {
    tmpDate = new Date(ts * 1000);
    //  tmpStr = tmpDate.toLocaleTimeString();

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

function populate_wifi_ssid_list_2() {
    console.log("populate_wifi_ssid_list_2");
    $.ajax({
        type: "GET",
        url: "/cache/wifis.json",
        cache: false,
        dataType: 'json',
        async: false,
        success: function (data) {
            wifi_ssid_list = document.getElementById('wifi_ssid_list');
            wifi_ssid_list.innerHTML = '';
            $.each(data, function (i, row) {
                console.log(row);
                addOption(wifi_ssid_list, row["id"], row["id"] + "(" + row["rssi"] + ")");
            });
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get wifis", textStatus, jqXHR.status, errorThrown);
        }
    });
}



function populate_wifi_ssid_list() {
    console.log("populate_wifi_ssid_list");
    launch_action("scan_wifis", '', {});
    setTimeout(function () { populate_wifi_ssid_list_2(); }, 5000); //when new data expected
}

//Scheduling functions

//TODO: get from main.cpp
const force_up_mins = [30, 60, 120, 180, 240, 360, 480, 600, 720, 960, 1200, 1440];


function post_schedule_update(channel_idx, duration, start, duration_old) {
    var scheds = [];
    scheds.push({ "ch_idx": channel_idx, "duration": duration, "from": start });
    console.log(scheds);

    $.ajax({
        type: "POST",
        url: "/update.schedule",
        cache: false,
        async: "false",
        data: JSON.stringify({ schedules: scheds }),
        contentType: "application/json",
        dataType: "json",
        success: function (data) {
            console.log(data);
            document.getElementById(`sch_${channel_idx}:duration`).value = (duration == 0) ? 60 : duration;
            document.getElementById(`sch_${channel_idx}:start`).value = 0;
            document.getElementById(`sch_${channel_idx}:save`).disabled = false; // should not be needed
        },
        error: function (errMsg) {
            console.log(errMsg);
            live_alert(`sch_${channel_idx}`, "Update failed", "warning");
        }
    });

}


// single channel schedule update
function schedule_update(evt) {
    channel_idx = get_idx_from_str(evt.target.id, 0);
    console.log("schedule_update channel_idx:", channel_idx);
    post_schedule_update(channel_idx, document.getElementById(`sch_${channel_idx}:duration`).value, document.getElementById(`sch_${channel_idx}:start`).value);
    console.log("next update_fup_duration_element");
    // update select list
    duration = document.getElementById(`sch_${channel_idx}:duration`).value;
    schedule_el = document.getElementById(`sch_${channel_idx}:save`);
    document.getElementById(`sch_${channel_idx}:save`).checked = false;
    //scheduled_ts = schedule_el.value;
    // update_fup_duration_element(channel_idx, 0, (duration > 0));
    update_fup_schedule_element(channel_idx);

    //schedule_el.disabled = true;
    // if (duration == 0 || scheduled_ts == 0)

    setTimeout(function () { update_status(false); }, 1000); //update UI
}



function update_fup_schedule_element(channel_idx, current_start_ts = 0) {
    //dropdown, TODO: recalculate when new hour 

    now_ts = Date.now() / 1000;
    sch_start_sel = document.getElementById(`sch_${channel_idx}:start`);
    sch_save_radio = document.getElementById(`sch_${channel_idx}:save`);
    sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`)


    if (g_config.ch[channel_idx]["type"] == 0) {
        sch_start_sel.disabled = true;
        sch_save_radio.disabled = true;
        sch_duration_sel.disabled = true;
        return;
    }


    prev_selected = sch_start_sel.value;
    //TODO: price and so on
    $("#fupfrom_" + channel_idx).empty();

    duration_selected = sch_duration_sel.value;

    if (duration_selected <= 0) {
        //  addOption(sch_start_sel, 0, "now ->", (duration_selected > 0));
        sch_start_sel.disabled = true;
        sch_save_radio.disabled = true;
        return;
    }
    sch_start_sel.disabled = false;
    sch_save_radio.disabled = false;

    first_next_hour_ts = parseInt(((Date.now() / 1000)) / 3600) * 3600 + 3600;
    start_ts = first_next_hour_ts;
    //
    //  addOption(sch_start_sel, 0, "now ->", (duration_selected > 0));
    cheapest_price = -VARIABLE_LONG_UNKNOWN;
    cheapest_ts = -1;
    cheapest_index = -1;

    remove_select_options(sch_start_sel);
    addOption(sch_start_sel, 0, "now &rarr;", (prev_selected == 0));

    for (k = 0; k < 24; k++) {
        end_ts = start_ts + (duration_selected * 60) - 1;
        segment_price = get_price_for_segment(start_ts, end_ts);
        if (segment_price < cheapest_price) {
            cheapest_price = segment_price;
            cheapest_ts = start_ts;
            cheapest_index = k;
        }

        if (segment_price != -VARIABLE_LONG_UNKNOWN)
            price_str = " &bull; " + segment_price.toFixed(1) + " c/kWh";
        else
            price_str = "";

        addOption(sch_start_sel, start_ts, get_time_string_from_ts(start_ts, false, true) + "-> " + price_str, (prev_selected == start_ts));
        start_ts += 3600;
    }
    if (cheapest_index > -1) {
        //  console.log("cheapest_ts", cheapest_ts)
        sch_start_sel.value = cheapest_ts;
        sch_start_sel.options[cheapest_index + 1].innerHTML = sch_start_sel.options[cheapest_index + 1].innerHTML + " ***";
    }
}

function duration_changed(evt) {
    channel_idx = get_idx_from_str(evt.target.id, 0);
    update_fup_schedule_element(channel_idx);
    document.getElementById(`sch_${channel_idx}:save`).disabled = false;
}

function delete_schedule(evt) {
    channel_idx = get_idx_from_str(evt.target.id, 0);
    post_schedule_update(channel_idx, 0, 0);
    document.getElementById(`sch_${channel_idx}:delete`).disabled = true;
    document.getElementById(`sch_${channel_idx}:start`).value = -1;
    setTimeout(function () { update_status(false); }, 1000); //update UI

}

//TODO: refaktoroi myös muut
function remove_select_options(select_element) {
    for (i = select_element.options.length - 1; i >= 0; i--) {
        select_element.remove(i);
    }
}


//End of scheduling functions

function load_application_config() {
    //current config
    $.ajax({
        type: "GET",
        cache: false,
        url: '/settings',
        dataType: 'json',
        async: false,
        success: function (data) { g_config = data; console.log("got g_config"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_config", textStatus, jqXHR.status, errorThrown);
        }
    });

    for (const property in g_config) {
        ctrl = document.getElementById(property);
        if (ctrl !== null) {
            if (ctrl.type.toLowerCase() == 'checkbox') {
                ctrl.checked = g_config[property];
            }
            else { // normal text 
                ctrl.value = g_config[property];
            }
        }
    }

    $.getJSON('/data/variable-info.json', function (data) {
        variable_list = data;
    });


};

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

function is_relay_id_used(channel_type) { // id required
    return [CH_TYPE_GPIO_FIXED, CH_TYPE_GPIO_USER_DEF, CH_TYPE_GPIO_USR_INVERSED, CH_TYPE_MODBUS_RTU].includes(parseInt(channel_type));
}
function is_relay_ip_used(channel_type) { //ip required

    return [CH_TYPE_SHELLY_1GEN, CH_TYPE_SHELLY_2GEN, CH_TYPE_TASMOTA].includes(parseInt(channel_type));
}
function is_relay_uid_used(channel_type) { //unit_id required
    if (is_relay_ip_used(parseInt(channel_type)))
        return true;
    return [CH_TYPE_MODBUS_RTU].includes(parseInt(channel_type));
}

function set_relay_field_visibility(channel_idx, ch_type) {
    document.getElementById(`ch_${channel_idx}:r_ip`).disabled = (!is_relay_ip_used(ch_type));
    document.getElementById(`ch_${channel_idx}:r_id`).disabled = (!is_relay_id_used(ch_type));
    document.getElementById(`ch_${channel_idx}:r_uid`).disabled = (!is_relay_uid_used(ch_type));

}


function set_channel_fields_relay_type(channel_idx, chtype_in) {
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
function get_idx_from_str(id_str, idx_nbr) {
    const fld_a1 = id_str.split(":");
    if (fld_a1.length <= idx_nbr)
        return -1;
    const fld_a2 = fld_a1[idx_nbr].split("_");
    if (fld_a2.length < 2)
        return -1;
    return parseInt(fld_a2[1]);
}

function set_channel_fields_by_type(evt, ch_idx) {
    var ch_type
    if (evt !== null) {
        ch_idx = get_idx_from_str(evt.target.id, 0);
        ch_type = evt.target.value;
    }
    else
        ch_type = document.getElementById(`ch_${ch_idx}:type`).value

    set_relay_field_visibility(ch_idx, ch_type);
    for (var t = 0; t < g_constants.RULE_STATEMENTS_MAX; t++) {
        $('#d_rc1_' + channel_idx + ' input').attr('disabled', (ch_type == CH_TYPE_UNDEFINED));
    }
}

var g_template_list = [];
var g_template_version = "";

function get_template_list() {
    if (g_template_list.length > 0)
        return;

    $.ajax({
        url: '/data/templates.json',
        dataType: 'json',
        async: false,
        success: function (data) {
            g_templates = data.templates;
            g_template_version = data.info.version;
            if (document.getElementById("version_templates"))
                document.getElementById("version_templates").innerHTML = g_template_version;

            $.each(data.templates, function (i, row) {
                if (row.hasOwnProperty("id")) // no version object 
                    g_template_list.push({ "id": row["id"], "name": _ltext(row, "name") });
            });
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get templates", textStatus, jqXHR.status);
        }
    });
}


function populateTemplateSel(selEl, template_id = -1) {
    get_template_list();
    if (selEl.options && selEl.options.length > 1) {
        console.log("already populated", selEl.options.length);
        return; //already populated
    }
    if (selEl.length == 0)
        addOption(selEl, -1, "Select template", false);
    // console.log("g_template_list.length", g_template_list.length);
    for (i = 0; i < g_template_list.length; i++) {
        addOption(selEl, g_template_list[i]["id"], g_template_list[i]["id"] + " - " + g_template_list[i]["name"], (template_id == g_template_list[i]["id"]));
    }

}

//KESKEN
function switch_rule_mode(channel_idx, rule_mode, reset, template_id) {
    template_id_ctrl = document.getElementById(`ch_${channel_idx}:template_id`);

    //experimental
    //template desc
    document.getElementById(`ch_${channel_idx}:desc`).innerHTML = "";
    //template rule desc
    for (let rule_idx = 0; rule_idx < g_constants.CHANNEL_CONDITIONS_MAX; rule_idx++) {
        document.getElementById(`ch_${channel_idx}:r_${rule_idx}:desc`).innerHTML = "";
    }

    if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE) { //template mode
        if (template_id_ctrl) {
            populateTemplateSel(template_id_ctrl, template_id);
            if (reset)
                template_id_ctrl.value = -1;
        }
        else
            console.log("Cannot find element:", template_id_ctrl);

        //set template and template rules desc:s
        $.each(g_templates, function (i, template) {
            if (template["id"] == template_id) {
                if (template.hasOwnProperty('desc'))
                    document.getElementById(`ch_${channel_idx}:desc`).innerHTML = _ltext(template, "desc");
                $.each(template.rules, function (rule_idx, rule) {
                    if (rule.hasOwnProperty('desc'))
                        document.getElementById(`ch_${channel_idx}:r_${rule_idx}:desc`).innerHTML = _ltext(rule, "desc");
                });
                return false;
            }
        });
    }
    else {
        document.getElementById(`ch_${channel_idx}:template_id`).value = -1;
        $('#rd_' + channel_idx + " input[type='radio']").attr('disabled', false);
    }

    rules_div = document.getElementById(`ch_${channel_idx}:rules`);

    //experimental
    $(`[id^=ch_${channel_idx}\\:r_] input[type='radio']`).attr('disabled', (rule_mode != 0)); //up/down

    $(`#ch_${channel_idx}\\:rules select`).attr('disabled', (rule_mode != 0));
    $(`#ch_${channel_idx}\\:rules input[type='text']`).attr('disabled', (rule_mode != 0)); //jos ei iteroi?
    $(`#ch_${channel_idx}\\:rules input[type='text']`).prop('readonly', (rule_mode != 0));

    //$('#rts_' + channel_idx).css({ "display": ((rule_mode == CHANNEL_CONFIG_MODE_RULE) ? "none" : "segment") });
    template_id_ctrl.disabled = (rule_mode == CHANNEL_CONFIG_MODE_RULE);

    //  $('#rtr_' + channel_idx).css({ "display": ((rule_mode == CHANNEL_CONFIG_MODE_RULE) ? "none" : "segment") });
    template_reset_ctrl = document.getElementById(`ch_${channel_idx}:template_reset`);
    template_reset_ctrl.disabled = (rule_mode == CHANNEL_CONFIG_MODE_RULE);

}


function delete_stmts_from_UI(channel_idx) {
    // empty vars/opers
    var selects_array = document.querySelectorAll("select[id^='ch_" + channel_idx + ":'][id$=':var'],select[id^='ch_" + channel_idx + ":'][id$=':oper']");
    console.log("delete_stmts_from_UI selects_array", selects_array.length);
    selects_array.forEach(var_select => {
        while (var_select.options.length > 0) {
            var_select.options.remove(0);
        }
        if (var_select.id.includes(":oper")) {
            var_select.classList.remove("visible");
            var_select.classList.add("invisible");
        }
    });

    var inputs_array = document.querySelectorAll("input[id^='ch_" + channel_idx + ":'][id$=':const']");
    inputs_array.forEach(const_select => {
        const_select.value = "";
        const_select.classList.remove("visible");
        const_select.classList.add("invisible");
    });



    // reset up/down checkboxes
    //for (let cond_idx = 0; cond_idx < g_constants.CHANNEL_CONDITIONS_MAX; cond_idx++) {
    //    set_radiob("ctrb_" + channel_idx + "_" + cond_idx, "0");
    //}
}


function changed_rule_mode(ev) {
    channel_idx = get_idx_from_str(ev.target.id, 0);

    rule_mode = ev.target.value;

    if (rule_mode == CHANNEL_CONFIG_MODE_RULE)
        confirm_text = "Change to advanced rule mode?";
    else
        confirm_text = "Change to template mode and delete current rule definations?";

    if (confirm(confirm_text)) {
        switch_rule_mode(channel_idx, rule_mode, true);
        if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE)
            delete_stmts_from_UI(channel_idx);// delete rule statements when initiating template mode
        return true;
    }
    else {
        //back to original        
        the_other_cb = document.getElementById(`ch_${channel_idx}:config_mode_${(1 - rule_mode)}`).checked = true; //advanced by default
        ev.target.checked = false;
        the_other_cb.checked = true;
        return false;
    }
}


function populateStmtField(channel_idx, rule_idx, stmt_idx, stmt = [-1, -1, 0, 0]) {
    //console.log("populateStmtField", channel_idx, rule_idx, stmt_idx, stmt);
    var_ctrl = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`);
    var_this = get_var_by_id(stmt[0]);
    populate_var(var_ctrl, stmt[0]);

    oper_ctrl = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:oper`);
    populate_oper(oper_ctrl, var_this, stmt);

    var_ctrl.value = stmt[0];
    oper_ctrl.value = stmt[1];
    document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`).value = stmt[3];
}


function addStmt(channel_idx, rule_idx = 1, stmt_idx = -1, stmt = [-1, -1, 0, 0]) {
    //get next statement index if not defined
    //TODO: maybe thsi does not work if elements already exist!
    if (stmt_idx == -1) {
        stmt_idx = 0;
        selector_text = "select[id^='ch_" + channel_idx + ":r_" + rule_idx + "'][id$=':var']";
        console.log(selector_text);
        let var_select_array = document.querySelectorAll(selector_text);
        for (let i = 0; i < var_select_array.length; i++) {
            if (var_select_array[i].options.length > 0 && var_select_array[i].value > -1)
                stmt_idx = Math.max(i + 1, stmt_idx);
        }
        /*    if (stmtDivs.length >= g_constants.RULE_STATEMENTS_MAX) {
                alert('Max ' + g_constants.RULE_STATEMENTS_MAX + ' statements allowed');
                return false;
            }*/
    }

    populateStmtField(channel_idx, rule_idx, stmt_idx, stmt);

}
function template_reset(ev) { //ch_0:template_reset
    channel_idx = get_idx_from_str(ev.target.id, 0);
    console.log(ev, "---", ev.target.id, "template_reset channel_idx", channel_idx);
    set_template_constants(channel_idx, false);
}

var template_data;


function set_template_constants(channel_idx, ask_confirmation) {
    template_id = document.getElementById(`ch_${channel_idx}:template_id`).value;
    if (template_id == -1) {
        if (confirm('Remove template definitions')) {
            delete_stmts_from_UI(channel_idx);
            return true;
        }
        else {
            $sel.val($sel.data('current')); //back to current
            return false;
        }
    }

    // delete_stmts_from_UI(channel_idx);

    $.each(g_templates, function (i, row) {
        if (row["id"] == template_id) {
            template_data = row;
            return false;
        }
    });

    $sel = $("#ch_" + channel_idx + "\\:template_id");

    $sel.data('current', $sel.val()); // now fix current value

    //one time is enough?, wait for confirmed results
    //TODO: here we couldhave first loop ceating modal for with fields and template description, possibly validation
    hasPrompts = false;
    form_fld_idx = 0;
    field_list = document.getElementById(`ch_${channel_idx}_tmpl_fields`);
    while (field_list.firstChild) {
        field_list.removeChild(field_list.lastChild);
    }
    // field_list.innerHTML = ""; //clear
    document.getElementById(`ch_${channel_idx}_tmpl_name`).innerHTML = _ltext(template_data, "name");
    document.getElementById(`ch_${channel_idx}_tmpl_desc`).innerHTML = _ltext(template_data, "desc");

    document.getElementById(`ch_${channel_idx}:desc`).innerHTML = "";
    //template rule desc
    for (let rule_idx = 0; rule_idx < g_constants.CHANNEL_CONDITIONS_MAX; rule_idx++) {
        document.getElementById(`ch_${channel_idx}:r_${rule_idx}:desc`).innerHTML = "";
    }

    if (template_data.hasOwnProperty('desc'))
        document.getElementById(`ch_${channel_idx}:desc`).innerHTML = _ltext(template_data, "desc");

    //Create modal form for parameters
    $.each(template_data.rules, function (rule_idx, rule) {
        if (rule.hasOwnProperty('desc')) {
            document.getElementById(`ch_${channel_idx}:r_${rule_idx}:desc`).innerHTML = _ltext(rule, "desc");
        }

        $.each(rule.stmts, function (stmt_idx, stmt_obj) {
            if (stmt_obj.length > 4 && stmt_obj[4].hasOwnProperty('prompt')) {
                hasPrompts = true;
                console.log(form_fld_idx, stmt_obj);

                const name_label = createElem("label", `ch_${channel_idx}_templ_field_${form_fld_idx}_name`, null, "form-label", null);
                name_label.setAttribute("for", `ch_${channel_idx}_templ_field_${form_fld_idx}_value`);
                var_this = get_var_by_id(stmt_obj[0]);
                name_label.innerHTML = var_this[1] + " (" + var_this[0] + ")  ";// variable name & id
                field_list.appendChild(name_label);

                const desc_span = createElem("span", `ch_${channel_idx}_templ_field_${form_fld_idx}_desc`, null, "text-muted", null);
                desc_span.innerHTML = "<br>" + _ltext(stmt_obj[4], "prompt"); //template variable prompt
                field_list.appendChild(desc_span);
                field_list.appendChild(document.createElement("br"));

                const value_input = createElem("input", `ch_${channel_idx}_templ_field_${form_fld_idx}_value`, stmt_obj[2], "form-control", "number");

                const value_in_rule_ctrl = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`);
                if (value_in_rule_ctrl.value.toString().length > 0) {
                    value_input.value = value_in_rule_ctrl.value;
                }

                if (stmt_obj[4].hasOwnProperty('min'))
                    value_input.setAttribute("min", stmt_obj[4].min);
                if (stmt_obj[4].hasOwnProperty('max'))
                    value_input.setAttribute("max", stmt_obj[4].max);
                if (stmt_obj[4].hasOwnProperty('step'))
                    value_input.setAttribute("step", stmt_obj[4].step);

                field_list.appendChild(value_input);
                field_list.appendChild(document.createElement("br"));
                form_fld_idx++;
            }

        });
    });

    if (hasPrompts) {
        var myModalEl = document.getElementById(`ch_${channel_idx}_tmpl_form`);
        var template_modal = bootstrap.Modal.getOrCreateInstance(myModalEl) // Returns a Bootstrap modal instance

        template_modal.show();
    }

    return true;
}


function changed_template(ev, selEl) {
    if (ev !== null) {
        channel_idx = get_idx_from_str(ev.target.id, 0);
    }
    // console.log("changed_template channel_idx", channel_idx);
    if (!set_template_constants(channel_idx, true)) {
        console.log("back to previous...");
    }
    return true;
}



//todo: data as parameter?
function populate_channel(channel_idx) {
    now_ts = Date.now() / 1000;

    //Dashboard scheduling
    ch_cur = g_config.ch[channel_idx];
    current_duration_minute = 0;
    current_start_ts = 0;
    has_forced_setting = false;
    if ((ch_cur.force_up_until > now_ts)) {
        has_forced_setting = true;
    }
    if ((ch_cur.force_up_from > now_ts)) {
        current_start_ts = ch_cur.force_up_from;
    }
    //color
    document.getElementById(`sch_${channel_idx}:title`).style.color = g_config.ch[channel_idx]["channel_color"];
    document.getElementById(`sch_${channel_idx}:label`).style["background-color"] = g_config.ch[channel_idx]["channel_color"];
    document.getElementById(`ch_${channel_idx}:title`).style.color = g_config.ch[channel_idx]["channel_color"];

    //experimental, is this enough or do we need loop
    //  update_fup_duration_element(channel_idx, current_duration_minute, has_forced_setting);
    update_fup_schedule_element(channel_idx, current_start_ts);
    /////sch_(ch#):card

    if (g_config.ch[channel_idx]["type"] == 0)
        document.getElementById(`sch_${channel_idx}:card`).classList.add("opacity-50")

    // end of scheduling

    document.getElementById(`sch_${channel_idx}:title`).innerText = g_config.ch[channel_idx]["id_str"];

    document.getElementById(`ch_${channel_idx}:title`).innerText = g_config.ch[channel_idx]["id_str"];
    document.getElementById(`ch_${channel_idx}:id_str`).value = g_config.ch[channel_idx]["id_str"];
    document.getElementById(`ch_${channel_idx}:channel_color`).value = (g_config.ch[channel_idx]["channel_color"]);

    document.getElementById(`ch_${channel_idx}:uptime_minimum_m`).value = parseInt(g_config.ch[channel_idx]["uptime_minimum"] / 60);
    document.getElementById(`ch_${channel_idx}:priority`).value = (g_config.ch[channel_idx]["priority"]);
    document.getElementById(`ch_${channel_idx}:load`).value = (g_config.ch[channel_idx]["load"]);

    populateTemplateSel(document.getElementById(`ch_${channel_idx}:template_id`), g_config.ch[channel_idx]["template_id"]);

    set_channel_fields_by_type(null, channel_idx);

    document.getElementById(`ch_${channel_idx}:r_ip`).value = g_config.ch[channel_idx]["r_ip"];
    document.getElementById(`ch_${channel_idx}:r_id`).value = g_config.ch[channel_idx]["r_id"];
    document.getElementById(`ch_${channel_idx}:r_uid`).value = g_config.ch[channel_idx]["r_uid"];


    document.getElementById(`ch_${channel_idx}:config_mode_0`).checked = (g_config.ch[channel_idx]["config_mode"] == 0);
    document.getElementById(`ch_${channel_idx}:config_mode_1`).checked = !(g_config.ch[channel_idx]["config_mode"] == 0);

    switch_rule_mode(channel_idx, g_config.ch[channel_idx]["config_mode"], false, g_config.ch[channel_idx]["template_id"]);


    if ("rules" in g_config.ch[channel_idx]) {
        for (rule_idx = 0; rule_idx < Math.min(g_config.ch[channel_idx]["rules"].length, g_constants.CHANNEL_CONDITIONS_MAX); rule_idx++) {
            this_rule = g_config.ch[channel_idx]["rules"][rule_idx];
            document.getElementById(`ch_${channel_idx}:r_${rule_idx}:up_0`).checked = this_rule["on"] ? false : true;
            document.getElementById(`ch_${channel_idx}:r_${rule_idx}:up_1`).checked = this_rule["on"] ? true : false;
            for (stmt_idx = 0; stmt_idx < Math.min(this_rule["stmts"].length, g_constants.RULE_STATEMENTS_MAX); stmt_idx++) {
                this_stmt = this_rule["stmts"][stmt_idx];

                populateStmtField(channel_idx, rule_idx, stmt_idx, this_stmt);

            }
        }
    }

}


function is_var_logical(constant_type) {
    return (constant_type >= 50 && constant_type <= 51);
}

function get_var_by_id(id) {
    for (var i = 0; i < g_constants.variables.length; i++) {
        if (g_constants.variables[i][0] == id) {
            return g_constants.variables[i];
        }
    };
}

function selected_var(ev) {
    /* sel_ctrl = ev.target;
     channel_idx = get_idx_from_str(sel_ctrl.id, 0);
     rule_idx = get_idx_from_str(sel_ctrl.id, 1);
     stmt_idx = get_idx_from_str(sel_ctrl.id, 2);*/

    var_this = get_var_by_id(ev.target.value);
    oper_ctrl = document.getElementById(ev.target.id.replace("var", "oper"));
    populate_oper(oper_ctrl, var_this);
    // show if hidden in the beginning
    oper_ctrl.classList.remove("invisible");
    oper_ctrl.classList.add("visible");

    //const_ctrl = document.getElementById(ev.target.id.replace("var", "const"));

    // console.log("variable", var_this, "is_var_logical(var_this[2])", is_var_logical(var_this[2]));
    //const_ctrl.style.display = (is_var_logical(var_this[2])) ? "none" : "segment"; //const-style

    //    if (g_constants.opers[i][0] == stmt[1]) {
    //      el_const.style.display = (g_constants.opers[i][5] || g_constants.opers[i][6]) ? "none" : "segment"; //const-style
    //  }

}

// operator select changed, show next statement fields if hidden
function selected_oper(el_oper) {
    // const el_oper = ev.target;
    channel_idx = get_idx_from_str(el_oper.id, 0);
    rule_idx = get_idx_from_str(el_oper.id, 1);
    stmt_idx = get_idx_from_str(el_oper.id, 2);
    el_const = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`);

    var show_const = false;
    if ((el_oper.value >= 0)) {
        // show initially hidden rules
        // if ((cond_idx + 1) < g_constants.CHANNEL_CONDITIONS_MAX) {
        //     document.getElementById("ru_" + channel_idx + "_" + (cond_idx + 1)).style.display = "segment";
        // }

        // set constant visibility (defined oper use no constants)
        // console.log("show_const params:", g_constants.opers[el_oper.value][5], g_constants.opers[el_oper.value][6])
        show_const = !(g_constants.opers[el_oper.value][5] || g_constants.opers[el_oper.value][6]);
        //   el_const.style.display = (g_constants.opers[el_oper.value][5] || g_constants.opers[el_oper.value][6]) ? "none" : "segment"; // const-style
    }

    // console.log(el_const.id,show_const);
    el_const.classList.remove(show_const ? "invisible" : "visible");
    el_const.classList.add(show_const ? "visible" : "invisible");
}
function selected_oper_ev(ev) {
    selected_oper(ev.target);
}

function populate_var(sel_ctrl, selected = -1) {
    // TODO: reads currently description from hardcoded source, could read variable-info variable_list
    if (sel_ctrl.options.length == 0) {
        addOption(sel_ctrl, -1, "-", false);
        for (var i = 0; i < g_constants.variables.length; i++) {
            var type_indi = is_var_logical(g_constants.variables[i][2]) ? "*" : " "; //logical
            var id_str = '(' + g_constants.variables[i][0] + ') ' + g_constants.variables[i][1] + type_indi;
            addOption(sel_ctrl, g_constants.variables[i][0], id_str, false); //(stmt[0] == g_constants.variables[i][0]));
        }
        // TODO: check that only one instance exists
        sel_ctrl.addEventListener("change", selected_var);
    }
    if (selected != -1)
        sel_ctrl.value = selected;
}

function focus_var(ev) {
    sel_ctrl = ev.target;
    populate_var(sel_ctrl);
}

function populate_oper(el_oper, var_this, stmt = [-1, -1, 0]) {
    channel_idx = get_idx_from_str(el_oper.id, 0);
    rule_idx = get_idx_from_str(el_oper.id, 1);
    stmt_idx = get_idx_from_str(el_oper.id, 2);

    if (el_oper.options.length <= 1) {
        el_oper.addEventListener("change", selected_oper_ev);
    }

    el_var = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`);
    el_const = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`);
    //console.log(el_oper.id, `ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`);

    el_const.value = stmt[2];

    if (el_oper.options.length == 0) {
        addOption(el_oper, -1, "", (stmt[1] == -1));
    }
    if (el_oper.options) {
        while (el_oper.options.length > 1) {
            el_oper.remove(1);
        }
    }

    //console.log(var_this);
    var show_constant = false;
    if (var_this) {
        //populate oper select
        //console.log("populate oper select, length ", g_constants.opers.length);
        for (let i = 0; i < g_constants.opers.length; i++) {
            if (g_constants.opers[i][6]) //boolean variable, defined/undefined oper is shown for all variables
                void (0); // do nothing, do not skip
            else if (is_var_logical(var_this[2]) && !g_constants.opers[i][5]) //boolean variable, not boolean oper
                continue;
            else if (!is_var_logical(var_this[2]) && g_constants.opers[i][5]) // numeric variable, boolean oper
                continue;
            el_oper.style.display = "segment";
            // constant element visibility

            addOption(el_oper, g_constants.opers[i][0], g_constants.opers[i][1], (g_constants.opers[i][0] == stmt[1]));
            if (g_constants.opers[i][0] == stmt[1]) {
                //  el_const.style.display = (g_constants.opers[i][5] || g_constants.opers[i][6]) ? "none" : "segment"; //const-style  
                show_constant = !(g_constants.opers[i][5] || g_constants.opers[i][6])
            }
        }
    }

    // if invisible
    el_oper.classList.remove("invisible");
    el_oper.classList.add("visible");

    //*** */
    if (typeof var_this != "undefined")
        show_constant = !is_var_logical(var_this[2]);
    else
        show_constant = true;

    selected_oper(el_oper);
    //console.log(el_oper.id,show_constant);


    rule_mode = document.getElementById(`ch_${channel_idx}:config_mode_0`).checked ? 0 : 1;
    el_oper.disabled = (rule_mode != 0);
    el_var.disabled = (rule_mode != 0);
    el_const.disabled = ((rule_mode != 0) || !show_constant);
}

function focus_oper(ev) {
    sel_ctrl = ev.target;
}

// Template field modal form for is closed and channel rules are updated with given values 
function template_form_closed(ev) {
    id_a = ev.target.id.split("_");
    channel_idx = id_a[1];
    delete_stmts_from_UI(channel_idx);
    form_fld_idx = 0;
    entered_field_values = {}; // given values for copying to other statements, NOTE! A prompt value must be before target in the statement
    console.log("******** adding new fields");
    // assume global variable template_data.conditions populated  when creating the form
    $.each(template_data.rules, function (cond_idx, rule) {
        //  console.log("cond_idx, rule", cond_idx, rule);
        document.getElementById(`ch_${channel_idx}:r_${cond_idx}:up_0`).checked = !rule["on"];
        document.getElementById(`ch_${channel_idx}:r_${cond_idx}:up_1`).checked = rule["on"];

        $.each(rule.stmts, function (j, stmt_obj) {
            // stmt_obj = stmt;
            new_value = stmt_obj[2];

            // user entered value handling
            if (stmt_obj.length > 4) {
                console.log("Used interaction field,  stmt_obj", stmt_obj);
                if (stmt_obj[4].hasOwnProperty('prompt')) {
                    if (document.getElementById(`ch_${channel_idx}_templ_field_${form_fld_idx}_value`)) {
                        new_value = document.getElementById(`ch_${channel_idx}_templ_field_${form_fld_idx}_value`).value;
                        if (stmt_obj[4].hasOwnProperty('field_id'))
                            entered_field_values[stmt_obj[4].field_id] = new_value;
                        console.log("*******new value", `ch_${channel_idx}_templ_field_${form_fld_idx}_value`, new_value);
                        form_fld_idx++;
                    }
                    else
                        console.log(`no element ch_${channel_idx}_templ_field_${form_fld_idx}_value`, stmt_obj[3]);
                }
                else if (stmt_obj[4].hasOwnProperty('copy_from_field')) {
                    console.log("Copying value from field", stmt_obj[4].copy_from_field);
                    multiplier = stmt_obj[4].hasOwnProperty('copy_multiplier') ? stmt_obj[4].copy_multiplier : 1;
                    if (entered_field_values.hasOwnProperty(stmt_obj[4].copy_from_field)) {
                        new_value = entered_field_values[stmt_obj[4].copy_from_field] * multiplier;
                        console.log("New value from the other ", new_value);
                    }
                    else
                        console.log("Cannot copy field value. No field with id ", stmt_obj[4].field_id);
                }
            }


            stmt_obj2 = [stmt_obj[0], stmt_obj[1], null, new_value];
            console.log("before addStmt: ", channel_idx, cond_idx, stmt_obj, stmt_obj2);
            addStmt(channel_idx, cond_idx, j, stmt_obj2);
        });
    });
    var template_modal = bootstrap.Modal.getOrCreateInstance(document.getElementById(`ch_${channel_idx}_tmpl_form`));

    document.getElementById(`ch_${channel_idx}:save`).disabled = false; //todo: could check if parameters have changed

    template_modal.hide();
}
function do_backup() {
    console.log("do_backup");
    $.fileDownload('/setting?format=file')
        .done(function () {
            ;
            // alert('File download a success!');
        })
        .fail(function () { alert('File download failed!'); });
}

/* WiP
function reload_when_site_up_again(require_version_base) {
    $.ajax({
        url: '/application',
        dataType: 'json',
        async: true,
        timeout: 3000,
        success: function (data) {
            version = data.version.split(" ")[0];
            version_fs = data.version_fs.split(" ")[0];
            if (require_version_base.length>0) {
                if (!version.startsWith(require_version_base) || !version_fs.startsWith(require_version_base)) {
                    setTimeout(function () { reload_when_site_up_again(require_version_base); }, 5000);
                    console.log("The site is up but versions don't match, reloading", require_version_base, version, version_fs);
                    return;
                }
            }
            console.log("The site is up, reloading", version, version_fs);
            //href_a = window.location.href.split("?");
           // window.location.href = href_a[0] + "?rnd=" + Math.floor(Math.random() * 1000);
            location.reload();
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("The site is down. Retrying in ", loop_freq);
            setTimeout(function () { reload_when_site_up_again(require_version_base); }, 5000);
        }
    });
}
*/

function check_restart_request(data, card_id) {
    if ("refresh" in data) {
        if (data.refresh > 0) {
            console.log("Reloaded in ", data.refresh, "seconds");
            setTimeout(function () { location.reload(); }, data.refresh * 1000);
            live_alert("config", "Updating system...wait patiently...", 'success');
            document.getElementById("config_spinner").style.display = "block";
            // $("#config_spinner").show();
        }
    }
}


//https://stackoverflow.com/questions/14446447/how-to-read-a-local-text-file-in-the-browser
// restore config (with reset)
var restore_config = function (event) {
    var input = event.target;
    var reader = new FileReader();
    reader.onload = function () {
        var text = reader.result;
        //  var node = document.getElementById('output');
        //  node.innerText = text;
        console.log(reader.result.substring(0, 200));
        if (!confirm("Restore from settings from the file and overwrite current settings. The system will restart after the restore.\r\rAre you sure?"))
            return;

        $.ajax({
            url: "/settings-restore", //window.location.pathname,
            cache: false,
            type: 'POST',
            //  contentType: "application/json;charset=ISO-8859-1",
            contentType: "application/json",
            data: text,
            processData: false,
            success: function (data) {
                // alert(data)
                //console.log("posted", text);
                live_alert("config", "Settings restored", 'success');
                check_restart_request(data, "config");
            },
            error: function (jqXHR, textStatus, errorThrown) {
                console.log("Cannot post config", textStatus, jqXHR.status);
                live_alert("config", "Cannot restore settings.", 'warning');
            },
            cache: false,
            contentType: false,
            processData: false
        });
    };
    reader.readAsText(input.files[0]);
};


function create_channels() {
    console.log("create_channels");

    //front page 
    for (channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) { //
        //  console.log("creating scheduling for ch " + channel_idx);
        document.getElementById("schedules").insertAdjacentHTML('beforeend', schedule_html.replaceAll("sch_(ch#)", "sch_" + channel_idx));
    }
    /*   for (channel_idx = g_constants.CHANNEL_COUNT - 1; channel_idx > -1; channel_idx--) { //beforeend
           document.getElementById("schedules").insertAdjacentHTML('afterbegin', schedule_html.replaceAll("sch_(ch#)", "sch_" + channel_idx).replaceAll("(ch#)", channel_idx));
       }*/


    // channel settings page
    channels = document.getElementById("channels_row");
    for (channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) {
        channels.insertAdjacentHTML('beforeend', channel_html.replaceAll("(ch#)", channel_idx));

        // populate types, "channel_types": [
        //    `ch_${channel_idx}:type`
        channel_type_ctrl = document.getElementById(`ch_${channel_idx}:type`);
        //  console.log(`ch_${channel_idx}:type`);
        for (var i = 0; i < g_constants.channel_types.length; i++) {
            addOption(channel_type_ctrl, g_constants.channel_types[i].id, g_constants.channel_types[i].name, (g_config.ch[channel_idx]["type"] == g_constants.channel_types[i].id));
        }

        sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`);
        if (channel_idx < (g_config.ch.length)) { // we should have data
            //initiate rule structure
            rule_list = document.getElementById(`ch_${channel_idx}:rules`);
            for (rule_idx = 0; rule_idx < g_constants.CHANNEL_CONDITIONS_MAX; rule_idx++) {
                rule_id = `ch_${channel_idx}:r_${rule_idx}`;
                rule_list.insertAdjacentHTML('beforeend', rule_html.replaceAll("ch_#:r_#", rule_id));
                document.getElementById(`${rule_id}:title`).innerText = "Rule " + (rule_idx + 1);

                stmt_list = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:stmts`);
                for (stmt_idx = 0; stmt_idx < g_constants.RULE_STATEMENTS_MAX; stmt_idx++) {
                    stmt_id = `ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}`;
                    stmt_list.insertAdjacentHTML('beforeend', stmt_html.replaceAll("ch_#:r_#:s_#", stmt_id));

                    // lets try lazy population, var, oper, const
                    document.getElementById(`${stmt_id}:var`).addEventListener("focus", focus_var);
                    document.getElementById(`${stmt_id}:oper`).addEventListener("focus", focus_oper);
                }
            }

            // schedule controls
            remove_select_options(sch_duration_sel);
            if (g_config.ch[channel_idx]["type"] != 0) { // only if relay defined
                for (i = 0; i < force_up_mins.length; i++) {
                    min_cur = force_up_mins[i];
                    duration_str = pad_to_2digits(parseInt(min_cur / 60)) + ":" + pad_to_2digits(parseInt(min_cur % 60));
                    addOption(sch_duration_sel, min_cur, duration_str, min_cur == 60); //check checked
                }
            }

            // populate data, delayed
            //populate_channel(channel_idx);
        }

        //Schedulings listeners
        sch_duration_sel.addEventListener("change", duration_changed);

        sch_duration_del = document.getElementById(`sch_${channel_idx}:delete`);
        sch_duration_del.addEventListener("click", delete_schedule);

        channel_type_ctrl.addEventListener("change", set_channel_fields_by_type);

        // refine template constants

        template_reset_ctrl = document.getElementById(`ch_${channel_idx}:template_reset`);
        template_reset_ctrl.addEventListener("click", template_reset);

        //modal closing
        document.getElementById(`ch_${channel_idx}_tmpl_close`).addEventListener("click", template_form_closed);
        document.getElementById(`ch_${channel_idx}:template_id`).addEventListener("change", changed_template);
    }
    let cm_buttons = document.querySelectorAll("input[id*=':config_mode_']");

    for (let i = 0; i < cm_buttons.length; i++) {
        cm_buttons[i].addEventListener("click", changed_rule_mode);
    }


    if (typeof feather != "undefined") {
        feather.replace(); // this replaces  <span data-feather="activity">  with svg
    }
    else {
        console.log("Feather icons undefined.");
    }
}

function populate_channels() {
    for (channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) {
        populate_channel(channel_idx);
    }

}
function jump(section_id_full) {
    //onclick="activate_section("channels:ch_4:r_0");"
    url_a = section_id_full.split(":");
    section_id = url_a[0];

    console.log("Jumping to section " + section_id);
    $('#' + section_id + '-tab').trigger('click');
    // show new section div….
    let section_divs = document.querySelectorAll("div[id^='section_']");
    for (i = 0; i < section_divs.length; i++) {
        is_active_div = section_divs[i].id == "section_" + section_id;
        section_divs[i].style.display = (is_active_div ? "block" : "none");
    }

    if (section_id == 'channels') {

        if (section_id == "channels" && url_a.length == 3) {
            rule_accordion = document.getElementById(`${url_a[1]}_colla_rules`);
            rule_accordion.classList.remove("collapse");
            rule_accordion.classList.remove("open");

            var scroll_element = document.getElementById(`${url_a[1]}:${url_a[2]}:title`);
            console.log("Try to scroll", scroll_element.id);

            if (scroll_element)
                scroll_element.scrollIntoView();

        }
    }
    else if (section_id == 'admin') {
        console.log("Scrolling to ", `${url_a[1]}:title`);
        var scroll_element = document.getElementById(`${url_a[1]}:title`);
        if (scroll_element)
            scroll_element.scrollIntoView();
        else
            console.log("Scrolling element not found.");
    }
}


// for sorting wifis by signal strength
function compare_wifis(a, b) {
    return ((a.rssi > b.rssi) ? -1 : 1);
}
/*
function initWifiForm() {
    var wifisp = null;
    $.ajax({
        url: '/cache/wifis.json',
        cache: false,
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
*/
function init_ui() {
    console.log("Init ui");
    get_price_data(); //TODO: also update at 2pm
    $.ajax({
        url: '/application',
        dataType: 'json',
        async: false,
        success: function (data) {
            g_constants = data; console.log("got g_constants");
            // versions
            let version_str = "Current version: " + g_constants.VERSION;
            if (g_constants.VERSION.localeCompare(g_constants.version_fs) != 0) {
                version_str += ` <span class="text-warning">Filesystem version '${g_constants.version_fs}' differs!</span>`;
            }
            document.getElementById("version").innerHTML = version_str;
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_constants", textStatus, jqXHR.status);
        }
    });


    load_application_config();

    create_channels();

    save_buttons = document.querySelectorAll("[id$=':save']");
    for (let i = 0; i < save_buttons.length; i++) {
        if (save_buttons[i].id.startsWith("ch_"))
            save_buttons[i].addEventListener("click", save_channel);
        else if (save_buttons[i].id.startsWith("sch_"))
            save_buttons[i].addEventListener("change", schedule_update);
        else
            save_buttons[i].addEventListener("click", save_card);
        if (!(save_buttons[i].id.startsWith("sch_"))) {
            save_buttons[i].disabled = true;
            console.log("save_buttons[i].disabled");
        }

    }

    action_buttons = document.querySelectorAll("button[id^='admin:']");
    for (let i = 0; i < action_buttons.length; i++) {
        action_buttons[i].addEventListener("click", launch_action_evt);
    }

    //event listeners for controls for enablin save etc
    const input_controls = document.querySelectorAll("input, select");
    for (let i = 0; i < input_controls.length; i++) {
        //  should be input to catch changes before leaving the input field
        if (input_controls[i].type == "radio")
            input_controls[i].addEventListener("click", ctrl_changed);
        else
            input_controls[i].addEventListener("change", ctrl_changed);
    }

    // delay loading
    var script = document.createElement('script');
    script.src = "/js/chart.min.3.9.1.js";
    document.getElementsByTagName('body')[0].appendChild(script);

    setTimeout(function () { populate_channels(); update_status(true); }, 2 * 1000);
    setTimeout(function () { create_dashboard_chart(); }, 45000); //one time, hopefully with all data

    const tooltipTriggerList = document.querySelectorAll('[data-bs-toggle="tooltip"]');
    const tooltipList = [...tooltipTriggerList].map(tooltipTriggerEl => new bootstrap.Tooltip(tooltipTriggerEl));
}

function live_alert(card_idstr, message, type) {
    alertPlaceholder = document.getElementById(card_idstr + ':alert');
    if (alertPlaceholder !== null) {
        const wrapper = document.createElement('div');
        alert_id = card_idstr + ':alert_i';
        wrapper.innerHTML = [
            `<div id="${alert_id}" class="alert alert-${type} alert-dismissible" role="alert">`,
            `   <div>${message}</div>`,
            '   <button type="button" class="btn-close" data-bs-dismiss="alert" aria-label="Close"></button>',
            '</div>'
        ].join('');
        alertPlaceholder.innerHTML = ""; // do not cumulate messages
        alertPlaceholder.append(wrapper);
    }
    else { // no placeholder
        alert(message + " . " + card_idstr + ':alert');
    }
}
/*
const alertTrigger = document.getElementById('liveAlertBtn')
if (alertTrigger) {
  alertTrigger.addEventListener('click', () => {
    alert('Nice, you triggered this alert message!', 'success')
  })
} */


function find_pid(el, id) {
    var p = el;
    while (p = p.parentNode)
        if (p.id && p.id == id)
            return p;
    return null;
}
function find_parent_card(el) {
    var p = el;
    while (p = p.parentNode) {

        var colon_count = (p.id.match(/:/g) || []).length; //filter out rule sub cards 
        if (p.id && p.id.endsWith(":card") && colon_count == 1) {
            //   console.log("returns:",p.id.replace(":card", ""));
            return p.id.replace(":card", "");
        }
    }
    console.log("no parent found");
    return "";
}
// enables save button after content change
function ctrl_changed(ev) {
    let parent_card = find_parent_card(ev.target);
    console.log("ctrl_changed", ev.target.id, parent_card ? parent_card : "no parent card");
    if (parent_card) {
        save_el = document.getElementById(parent_card + ":save");
        if ((save_el !== null))
            save_el.disabled = false;
    }
}

function launch_action(action, card_id, params) {
    //  var post_data = { "action": action };
    let post_data = Object.assign({ "action": action }, params);
    console.log("post_data", post_data);
    if (card_id)
        live_alert(card_id, "Sending:\n" + JSON.stringify(post_data), 'success');

    $.ajax({
        type: "POST",
        url: "/actions",
        cache: false,
        async: "false",
        data: JSON.stringify(post_data),
        contentType: "application/json;",
        dataType: "json",
        success: function (data) {
            console.log("action response", data);
            if (card_id)
                live_alert(card_id, "Action launched", 'success');
            
            // could poll status / version and reboot 
            //if (action == "update")
            //    setTimeout(function () { reload_when_site_up_again(params.version); }, 5000);
            
            check_restart_request(data, card_id);
        },
        error: function (requestObject, error, errorThrown) {
            if (card_id)
                live_alert(card_id, "Action failed: " + error + ", " + errorThrown, 'warning');
        }
    });

}
function launch_action_evt(ev) {
    id_a = ev.target.id.split(":");
    card = id_a[0]; // should be "admin"
    action = id_a[1];
    if (action == "restart" && !confirm("Do you want to restart the system?"))
        return;
    if (action == "reset" && !confirm("Take a backup before reset. Do you want reset all system settings? "))
        return;
    launch_action(action, card, {});
}

function start_fw_update() {
    new_version = document.getElementById("sel_releases").value;
    if (new_version.length < 2) {
        alert("Refresh available software versions and select version from the list.");
        return;
    }

    console.log("new_version, g_constants.VERSION_SHORT", new_version, g_constants.VERSION_SHORT);
    if (g_constants.VERSION_SHORT.startsWith(new_version)) {
        if (!confirm("Firmware version is already " + g_constants.VERSION_SHORT + ". Do you want to reinstall?"))
            return;
    }
    if (confirm("Backup your configuration before update!\r\r Update firmware to " + new_version + " now?")) {
        $('#releases\\:update').prop('disabled', true);
        launch_action("update", "", { "version": new_version });
    }
    else
        return;

}


function save_channel(ev) {
    channel_idx = get_idx_from_str(ev.target.id, 0);
    if (channel_idx == -1) {
        alert("Invalid channel idx. System error, ev.target.id:" + ev.target.id);
        return;
    }
    var data_ch = { "idx": channel_idx };
    card = "ch_" + channel_idx
    console.log("save_channel channel_idx", channel_idx, ev.target.id);

    data_ch["id_str"] = document.getElementById(`ch_${channel_idx}:id_str`).value;
    data_ch["uptime_minimum"] = parseInt(document.getElementById(`ch_${channel_idx}:uptime_minimum_m`).value * 60);
    data_ch["load"] = document.getElementById(`ch_${channel_idx}:load`).value;

    data_ch["channel_color"] = document.getElementById(`ch_${channel_idx}:channel_color`).value;
    data_ch["priority"] = document.getElementById(`ch_${channel_idx}:priority`).value;

    data_ch["template_id"] = parseInt(document.getElementById(`ch_${channel_idx}:template_id`).value);

    data_ch["type"] = parseInt(document.getElementById(`ch_${channel_idx}:type`).value);

    data_ch["r_ip"] = document.getElementById(`ch_${channel_idx}:r_ip`).value;
    data_ch["r_id"] = parseInt(document.getElementById(`ch_${channel_idx}:r_id`).value);
    data_ch["r_uid"] = parseInt(document.getElementById(`ch_${channel_idx}:r_uid`).value);

    data_ch["config_mode"] = parseInt(document.getElementById(`ch_${channel_idx}:config_mode_0`).checked ? 0 : 1);

    rules = [];

    for (rule_idx = 0; rule_idx < g_constants.CHANNEL_CONDITIONS_MAX; rule_idx++) {
        stmt_count = 0;
        rule_stmts = [];
        up_value = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:up_0`).checked ? false : true;

        for (stmt_idx = 0; stmt_idx < g_constants.RULE_STATEMENTS_MAX; stmt_idx++) {
            var_value = parseInt(document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`).value);
            oper_value = parseInt(document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:oper`).value);
            const_value = parseFloat(document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`).value);

            if ((!isNaN(parseInt(var_value))) && (var_value > -1) && (!isNaN(parseInt(oper_value))) && (oper_value > -1)) {
                rule_stmts.push([var_value, oper_value, null, const_value]);
                //      console.log("A", [var_value, oper_value, null, const_value])
                stmt_count++;
            }
        }
        if (rule_stmts.length > 0)
            rules.push({ "on": up_value, "stmts": rule_stmts });
    }
    data_ch["rules"] = rules;

    const post_data = { "ch": [data_ch] };

    // testing a BIG message
    /*
    for (i = 0; i < parseInt(document.getElementById(`ch_0:uptime_minimum_m`).value ); i++) {
        post_data['property_' + i] = i;
    }
    */

    //POST
    console.log("sending post_data", post_data);

    $.ajax({
        type: "POST",
        url: "/settings",
        cache: false,
        async: "false",
        data: JSON.stringify(post_data),
        contentType: "application/json;",
        dataType: "json",
        success: function (data) {
            live_alert(card, "Updated", 'success');
            console.log("success, card", card, data);
            document.getElementById(card + ":save").disabled = true;
            console.log("save_channel save disabled");
        },
        error: function (requestObject, error, errorThrown) {
            console.log(error, errorThrown);
            live_alert(card, "Update failed: " + error + ", " + errorThrown, 'warning');
        }
    });
}

function toggle_show(el) {
    console.log(el.id);
    target_id = el.id.replace('_hide', '').replace('_show', '');
    target = document.getElementById(target_id);
    /*   is_show = target_id.includes('_show');
       the_other_id = target_id + (is_show ? "_hide" : "_show");
       console.log(target_id,"#", the_other_id);
       the_other_el = document.getElementById(the_other_id);
   */
    var show_eye = document.getElementById(target_id + "_show");
    var hide_eye = document.getElementById(target_id + "_hide");

    hide_eye.classList.remove('d-none');

    if (target.type === "password") {
        target.type = "text";
        show_eye.style.display = "none";
        hide_eye.style.display = "block";
    } else {
        target.type = "password";
        show_eye.style.display = "block";
        hide_eye.style.display = "none";
    }


    // el.classList.add('d-none');
    // el.classList.remove('d-none');
    // the_other_el.classList.remove('d-none');


}

function save_card(ev) {
    id_a = ev.target.id.split(":");
    card = id_a[0];
    var post_data = {};

    card_div = document.getElementById(card + ":card");

    let elems = card_div.querySelectorAll('input, select');
    for (let i = 0; i < elems.length; i++) {
        console.log("POST:" + elems[i].id + ", " + elems[i].type.toLowerCase())
        if (elems[i].id == 'http_password') {
            // console.log(elems[i].id, elems[i].value, document.getElementById('http_password2').value);
            if (elems[i].value != document.getElementById('http_password2').value) {
                live_alert(card, "Passwords don't match", "warning");
                return;
            }
        }

        if (elems[i].type.toLowerCase() == 'checkbox') {
            post_data[elems[i].id] = elems[i].checked;
        }
        else {
            post_data[elems[i].id] = elems[i].value;
        }
    }

    live_alert(card, "Sending:" + JSON.stringify(post_data), 'success');

    $.ajax({
        type: "POST",
        url: "/settings",
        cache: false,
        async: "false",
        data: JSON.stringify(post_data),
        contentType: "application/json",
        dataType: "json",
        success: function (data) {
            document.getElementById(card + ":save").disabled = true;
            if (["metering", "production", "network"].includes(card)) {
                let do_restart = confirm("Settings updated. Do you want to restart?");
                if (do_restart) {
                    launch_action("restart", card, {});
                    return;
                }
            }
            live_alert(card, "Settings updated", 'success');
            console.log("success, card ", card, data);
        },
        error: function (requestObject, error, errorThrown) {
            console.log(error, errorThrown);
            live_alert(card, "Update failed: " + error + ", " + errorThrown, 'warning');
        }
    });




}

