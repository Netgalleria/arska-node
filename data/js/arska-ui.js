var g_config;
var g_datamapping;
var g_constants;

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
const CH_TYPE_DISCOVERED = 1000; // pseudo type, use discovered device list

window.onload = function () {
    init_ui();
}

const schedule_html = `<div class="col"><div class="card white-card" id="sch_(ch#):card">
                  <div class="card-header d-flex align-items-center justify-content-between">
                    <h5 class="card-title m-lg-0 p-1 channel-color-(ch#)">
                        <span
                        class="channel-label channel-label-(ch#)"></span><span id="sch_(ch#):title"></span></h5>
                        
                    <label id="sch_(ch#):status" class="btn btn-secondary btn-sm text-bg-success">
                      <span id="sch_(ch#):status_icon" data-feather="zap" class="align-text-bottom"></span>
                      <span id="sch_(ch#):status_txt"></span></label>
                  </div>
                  <!--card-header-->
                  <div class="card-body">
                  <span class="text-muted">Manual scheduling</span>
                    <div class="input-group mb-3">
                      <span class="input-group-text" id="basic-addon1">Current</span>
                      <span class="input-group-text col-lg-6" id="sch_(ch#):current">-</span>
                      <input id="sch_(ch#):delete" type="radio" class="btn-check p-0"
                        autocomplete="off" value="0">
                      <label class="btn btn-secondary" for="sch_(ch#):delete">
                        <span data-feather="delete" class="align-text-bottom"></span>
                        </label>
                    </div>
                    <!--./input-group-->
                    <div class="input-group mb-3">
                      <span class="input-group-text" id="basic-addon1">Add</span>
                      <div class="col-md-6 col-lg-3">
                        <select id="sch_(ch#):duration" class="form-select" aria-label="variable" data-toggle="tooltip" title="Duration of the schedule hh:mm">
                        </select>
                      </div>
                      <!--./col-->
                      <div class="col-md-6 col-lg-6">
                        <select id="sch_(ch#):start" class="form-select" aria-label="variable">
                        <option value="1-">start</option>
                          <option value="0">now &rarr;</option>
                        </select>
                      </div>
                      <!--./col-->
                      <span class="input-group-text p-0" id="basic-addon1"> </span>
                      <input id="sch_(ch#):save" type="radio" class="btn-check" name="sch_(ch#):config_mode" autocomplete="off"
                        value="0">
                      <label class="btn btn-secondary" for="sch_(ch#):save">
                        <span data-feather="plus" class="align-text-bottom"></span>
                        </label>
                    </div>
                    <!--./input-group-->
                    <div id="sch_(ch#):alert">
                    </div>
                  </div>
                  <!--./card-body-->
                </div></div>`;


const channel_html = `<div class="row row-cols-1">
<div class="col">
    <!-- boiler
================================================== -->
    <!-- channel starts -->
    <div class="card white-card mb-3" id="ch_#:card">
        <div class="card-header">
            <h5 id="ch_#:title" class="card-title m-lg-0 p-1 channel-color-0"><span
                    class="channel-label channel-label-0"></span>Boiler</h5>
        </div>
        <div class="card-body pt-2">
            <!--basic settings-->
            <div class="form-section">
                <h6 class="form-section-title">Basic Settings</h6>
                <div class="row g-3">
                    <div class="col-12 col-md-6">
                        <label for="ch_#:id_str" class="form-label">Channel id</label>
                        <input id="ch_#:id_str" type="text" class="form-control"
                            placeholder="Channel name" maxlength="20">
                    </div>
                    <!--./col-->
                    <div class="col-auto">
                        <label for="ch_#:uptime_minimum_m" class="form-label">Minimum uptime
                            (minutes)</label>
                        <input id="ch_#:uptime_minimum_m" type="number" class="form-control" placeholder="5" min="0" step="1" max="480" >
                    </div>
                    <!--./col-->
                    <div class="col-6 col-lg-auto">
                        <label for="ch_#:channel_color" class="form-label">Channel
                            color</label>
                        <input type="color" class="form-control form-control-color"
                            id="ch_#:channel_color" value="#005d85" data-bs-toggle="tooltip"
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
                        <label for="ch_#:type" class="form-label">Relay type:</label>
                        <select id="ch_#:type" class="form-select" aria-label="variable">
                        </select>
                    </div>
                    <!--./col-->
                    <div id="ch_#:r_id_div" class="col-md-3">
                        <label for="ch_#:r_id" class="form-label">GPIO:</label>
                        <input id="ch_#:r_id" type="number" class="form-control" disabled=""  placeholder="33" min="0" step="1" max="33">
                       
                    </div>
                    <!--./col-->
                    <div id="ch_#:r_ip_div" class="col-md-4">
                        <label for="ch_#:r_ip" class="form-label">IP Address:</label>
                        <input id="ch_#:r_ip" type="text" class="form-control"
                            placeholder="0.0.0.0" required=""
                            pattern="((^|.)((25[0-5])|(2[0-4]d)|(1dd)|([1-9]?d))){4}$">
                    </div>
                    <!--./col-->
                    <div id="ch_#:r_uid_div" class="col-md-1">
                        <label for="ch_#:r_id" class="form-label">ID:</label>
                        <input id="ch_#:r_uid" type="number" class="form-control" placeholder="0" min="0" step="1" max="255">
                    </div>
                    <!--./col-->
                </div>
                <!--./row-->
            </div>
            <!--./form-section-->
            <!--Channel rules-->

            <div class="accordion" id="ch_#_accordion">
                <div class="accordion-item">
                    <h2 class="accordion-header" id="ch_#_accordionh3">
                        <button class="accordion-button accordion-title collapsed"
                            type="button" data-bs-toggle="collapse"
                            data-bs-target="#ch_#_colla3" aria-expanded="false"
                            aria-controls="ch_#_colla3">
                            Channel Rules
                        </button>
                    </h2>
                    
                    <div id="ch_#_colla3" class="accordion-collapse collapse"
                        aria-labelledby="ch_#_accordionh3" data-bs-parent="#ch_#_accordion"
                        style="">
                        <div class="accordion-body">
                            <div class="row g-3">
                                <div class="col">
                                    <div class="input-group mb-3">
                                        <span class="input-group-text"
                                            id="basic-addon3">Config mode</span>
                                        <input id="ch_#:config_mode_1" type="radio"
                                            class="btn-check" name="ch_#:config_mode"
                                            autocomplete="off" checked="" value="1">
                                        <label class="btn btn-secondary"
                                            for="ch_#:config_mode_1">
                                            <span data-feather="file"
                                            class="align-text-bottom"></span>Template</label>

                                        <input id="ch_#:config_mode_0" type="radio"
                                            class="btn-check" name="ch_#:config_mode"
                                            autocomplete="off" value="0">
                                        <label class="btn btn-secondary"
                                            for="ch_#:config_mode_0">
                                            <span data-feather="list" class="align-text-bottom"></span>
                                            Advanced</label>
                                    </div>
                                    <div class="input-group mb-3">
                                        <select id="ch_#:template_id" class="form-select"
                                            aria-label="variable" disabled="">
                                            <option value="-1">Select template</option>
                                        </select>
                                        <span class="input-group-text p-0"> </span>
                                        <button id="ch_#:template_reset" type="button"
                                            class="btn btn-primary" disabled="">
                                            <span data-feather="settings"
            class="align-text-bottom"></span>
                                        </button>
                                    </div>
                                </div>
                                <!--./col-->
                            </div>
                            <!--./row-->
                            <div id="ch_#:rules"
                                class="row row row-cols-1 row-cols-md-2 g-3">
                               
                                <!--./col-->
                            </div>
                            <!--./row-->


                        </div>

                    </div>
                </div> <!-- accordion -->
            </div>
            <div id="ch_#:alert"></div>

            <!-- Modal example channel rules
================================================== -->
            <div class="modal fade" id="exampleModalToggle" aria-hidden="true"
                aria-labelledby="exampleModalToggleLabel" tabindex="-1">
                <div class="modal-dialog modal-dialog-centered">
                    <div class="modal-content">
                        <div class="modal-header">
                            <h5 class="card-title mb-0" id="exampleModalToggleLabel">Create
                                Channel rule </h5>
                            <button type="button" class="btn-close" data-bs-dismiss="modal"
                                aria-label="Close"></button>
                        </div>
                        <div class="modal-body">
                            <p><strong>Template 2 - Hyödynnä halpa aurinkosähkö</strong></p>
                            <p>Päällä ainoastaan kun oman tuotannon ylijäämää. Kalliin
                                sähkön aikana myydään omaa
                                tuotantoa.</p>
                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-secondary"
                                data-bs-dismiss="modal">Cancel</button>
                            <button type="button" class="btn btn-primary"
                                data-bs-target="#exampleModalToggle2"
                                data-bs-toggle="modal">OK</button>
                        </div>
                    </div>
                </div>
            </div>
            <div class="modal fade" id="exampleModalToggle2" aria-hidden="true"
                aria-labelledby="exampleModalToggleLabel2" tabindex="-1">
                <div class="modal-dialog modal-dialog-centered">
                    <div class="modal-content">
                        <div class="modal-header">
                            <h5 class="card-title mb-0" id="exampleModalToggleLabel2">Create
                                Channel rule </h5>
                            <button type="button" class="btn-close" data-bs-dismiss="modal"
                                aria-label="Close"></button>
                        </div>
                        <div class="modal-body">
                            <p><strong>Template 2 - Hyödynnä halpa aurinkosähkö</strong></p>

                            <form>
                                <div class="mb-3">
                                    <label for="min-price" class="col-form-label">Min price
                                        to sell own production (e.g.
                                        13c/kWh)?</label>
                                    <input type="text" class="form-control" id="min-price">
                                </div>
                            </form>


                        </div>
                        <div class="modal-footer">
                            <button type="button" class="btn btn-secondary"
                                data-bs-dismiss="modal">Cancel</button>
                            <button type="button" class="btn btn-primary">OK</button>
                            <!--palaa takaisin ekaan vaiheeseen
<button class="btn btn-primary" data-bs-target="#exampleModalToggle2" data-bs-toggle="modal">OK</button>
-->
                        </div>
                    </div>
                </div>
            </div>
            <!-- modal example -->
            <a class="mt-3 d-block" data-bs-target="#exampleModalToggle"
                data-bs-toggle="modal">Example channel
                rules - modal example here:
                https://getbootstrap.com/docs/5.2/components/modal/#varying-modal-content
            </a>

        </div>
        <!--./card-body-->
        <div class="card-footer text-end">
            <button id="ch_#:save" class="btn btn-primary " type="submit" disabled="" >
            <span data-feather="save"
            class="align-text-bottom"></span>
                Save</button>
        </div>
        <!--./card-footer-->


    </div> <!-- channel ends-->

</div>
</div>
<!--./row-->`;

const rule_html = `<div class="col">
     <!-- rule starts -->
     <div class="card rule-card">
         <div class="card-header">
             <h5 id="ch_#:r_#:title" class="card-title">Rule
                 1</h5>
         </div>
         <div class="card-body">
             <div class="input-group mb-3">
                 <span class="input-group-text"
                     id="basic-addon3">Rule sets
                     channel</span>
                 <input id="ch_#:r_#:up_0" type="radio"
                     class="btn-check" name="ch_#:r_#:up"
                     autocomplete="off" checked="">
                 <label class="btn btn-secondary"
                     for="ch_#:r_#:up_0">
                     <span data-feather="zap-off"
            class="align-text-bottom"></span>
                     off</label>
                 <input id="ch_#:r_#:up_1" type="radio"
                     class="btn-check" name="ch_#:r_#:up"
                     autocomplete="off">
                 <label class="btn btn-secondary"
                     for="ch_#:r_#:up_1">
                     <span data-feather="zap"
            class="align-text-bottom"></span>
                     on</label>
             </div>
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
        <option value="-1"></option>

    </select>
</div>
<div class="col col-3">
    <input id="ch_#:r_#:s_#:const"
        type="text"
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

let last_status_update = 0;

// update variables and channels statuses to channels form
function update_status(repeat) {
    console.log("update_status starting");
    /*
    if (document.getElementById("statusauto"))
        show_variables = document.getElementById("statusauto").checked;
    else
        show_variables = false;
        */
    show_variables = false;
    //TODO: add variable output

    const interval_s = 30;
    const process_time_s = 5;
    let next_query_in = interval_s;

    now_ts = Date.now() / 1000;
    if (Math.floor(now_ts / 3600) != Math.floor(last_status_update / 3600)) {
        console.log("Interval changed in update_status");
        update_schedule_select_periodical(); //TODO:once after hour/period change should be enough
        price_chart_ok = update_price_chart();
        if (price_chart_ok)
            last_status_update = now_ts;
    }

    var jqxhr_obj = $.ajax({
        url: '/status',
        dataType: 'json',
        async: false,  //oli true
        success: function (data, textStatus, jqXHR) {
            console.log("got status data", textStatus, jqXHR.status);
            msgdiv = document.getElementById("msgdiv");
            keyfd = document.getElementById("keyfd");
            /*
                        has_import_values = false;
                        for (i = 0; i < data.net_exports.length; i++) {
                            if (Math.abs(data.net_exports[i]) > 1) {
                                has_import_values = true;
                                break;
                            }
            
                        }
                        if (has_import_values)
                            net_exports = data.net_exports;
                        //else
                        //    net_exports = [];
            */

            if (data.hasOwnProperty('next_process_in'))
                next_query_in = data.next_process_in + process_time_s;

            if (msgdiv) {
                if (data.last_msg_ts > getCookie("msg_read")) {
                    msgDateStr = get_time_string_from_ts(data.last_msg_ts, true, true);
                    msgdiv.innerHTML = '<span class="msg' + data.last_msg_type + '">' + msgDateStr + ' ' + data.last_msg_msg + '<button class="smallbtn" onclick="set_msg_read(this)" id="btn_msgread">✔</button></span>';
                }
            }
            /*
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
            */

            // TODO: update

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
            // TODO: update
            //console.log("Starting populate_channel_status loop");
            $.each(data.ch, function (i, ch) {
                populate_channel_status(i, ch)
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

    if (repeat) {
        setTimeout(function () { update_status(true); }, next_query_in * 1000);
        //   console.log("next_query_in", next_query_in)
    }
}


function populate_channel_status(channel_idx, ch) {
    // console.log("populate_channel_status",channel_idx, ch);
    //TODO: update this to new...

    now_ts = Date.now() / 1000;
    // console.log(channel_idx,ch);
    sch_current_span = document.getElementById(`sch_${channel_idx}:current`);
    sch_status_label = document.getElementById(`sch_${channel_idx}:status`);
    sch_status_icon_span = document.getElementById(`sch_${channel_idx}:status_icon`);
    sch_status_text_span = document.getElementById(`sch_${channel_idx}:status_txt`);

    // tsekkaa miten ikonin vaihto, pitääkö mennä svg:llä vai hide/show vai voisiko nykyisen svg:n korvata spanilla ja ajaa replace
    //if (status_changed)
    //    feather.replace(); //icon
    if ((ch.force_up_until > now_ts)) {
        sch_current_span.innerHTML = get_time_string_from_ts(ch.force_up_from, false, true) + " &rarr; " + get_time_string_from_ts(ch.force_up_until, false, true);
        //    console.log("sch_current_span.innerText", sch_current_span.innerText);
    }
    else
        sch_current_span.innerHTML = "-";

    //  <span id="sch_(ch#):status_txt">ON based on <a href="#" class="link-light">rule 2</a></span></label>

    sch_status_label.classList.remove(ch.is_up ? "text-bg-danger" : "text-bg-success");
    sch_status_label.classList.add(ch.is_up ? "text-bg-success" : "text-bg-danger");
    //console.log(" sch_status_label.classList", ch, ch.is_up, sch_status_label.classList);

    info_text = "";
    if (ch.active_condition > -1)
    rule_link_a = " onclick='activate_section(\"channels_c" + channel_idx + "r" + ch.active_condition + "\");'";

    if (ch.is_up) {
        if ((ch.force_up_from <= now_ts) && (now_ts < ch.force_up_until))
            info_text += "Up based on manual schedule.";
        else if (ch.active_condition > -1)
            info_text += "Up based on <a class='chlink' " + rule_link_a + ">rule " + (ch.active_condition + 1) + "</a>. ";
    }
    else {
        if ((ch.active_condition > -1))
            info_text += "Down based on <a class='chlink' " + rule_link_a + ">rule " + (ch.active_condition + 1) + "</a>. ";
        if ((ch.active_condition == -1))
            info_text += "Down, no matching rules. ";
    }

    sch_status_text_span.innerHTML = info_text;
    return;
}


function statusCBClicked(elCb) {
    if (elCb.id == 'statusauto') {
        if (elCb.checked) {
            setTimeout(function () { update_status(false); }, 300);
        }
        document.getElementById("variables").style.display = document.getElementById("statusauto").checked ? "block" : "none";
    }
    else if (elCb.id == 'cbShowPrices') {
        setCookie("show_price_graph", elCb.checked ? 1 : 0);
        update_price_chart();
    }
}


function link_to_wiki(article_name) {
    return '<a class="helpUrl" target="wiki" href="https://github.com/Netgalleria/arska-node/wiki/' + article_name + '">ℹ</a>';
}

function get_date_string_from_ts(ts) {
    tmpDate = new Date(ts * 1000);
    return tmpDate.getFullYear() + '-' + ('0' + (tmpDate.getMonth() + 1)).slice(-2) + '-' + ('0' + tmpDate.getDate()).slice(-2) + ' ' + tmpDate.toLocaleTimeString();
}
var prices = [];
var net_exports = [];
var prices_first_ts = 0;
var prices_last_ts = 0;
var prices_resolution_min = 60;
var prices_expires = 0;


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

function update_price_chart() {
    /*  if (!document.getElementById("cbShowPrices").checked) {
          document.getElementById("chart_container").style.display = "none";
          return;
      }
  */

    // experimental import query
    var jqxhr_obj = $.ajax({
        url: '/status',
        dataType: 'json',
        async: false,  //oli true
        success: function (data, textStatus, jqXHR) {
            has_import_values = false;
            console.log(data.variable_history, data.variable_history["103"], data.variable_history["103"].length);
            for (i = 0; i < data.variable_history["103"].length; i++) {
                if (Math.abs(data.variable_history["103"][i]) > 1) {
                    has_import_values = true;
                    break;
                }
            }
            if (has_import_values)
                net_exports = data.variable_history["103"];
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get status in price query", Date(Date.now()).toString(), textStatus, jqXHR.status);
        }
    });
    //**** 
    console.log("has_import_values", has_import_values, "net_exports", net_exports);

    let price_data = null;

    $.ajax({
        url: 'data/price-data.json',
        dataType: 'json',
        async: false,
        success: function (data, textStatus, jqXHR) {
            console.log('got data/price-data.json', textStatus, jqXHR.status);
            price_data = data;
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get prices", textStatus, jqXHR.status);
            document.getElementById("chart_container").style.display = "none";
        }
    });

    if (!price_data) {
        console.log("update_price_chart: no price data");
        return false;
    }

    const ctx = document.getElementById('day_ahead_chart').getContext('2d');

    let date;
    let time_labels = [];
    let prices_out = [];
    let imports = [];
    let idx = 0;
    let now_idx = 0;
    now_ts = (Date.now() / 1000);

    var tz_offset = new Date().getTimezoneOffset();
    start_date_str = ts_date_time(price_data.record_start, false) + ' - ' + ts_date_time(price_data.record_end_excl - 3600, false);

    for (ts = price_data.record_start; ts < price_data.record_end_excl; ts += (price_data.resolution_m * 60)) {
        if (ts > now_ts && now_idx == 0)
            now_idx = idx - 1;

        date = new Date(ts * 1000);
        day_diff_now = parseInt((ts - tz_offset * 60) / 86400) - parseInt(now_ts / 86400);
        if (day_diff_now != 0)
            day_str = " (" + ((day_diff_now < 0) ? "" : "+") + day_diff_now + ")";
        else
            day_str = " ";
        //     time_labels.push(pad_to_2digits(date.getDate()) + '.' + pad_to_2digits(date.getMonth() + 1) + '. ' + pad_to_2digits(date.getHours()) + ':' + pad_to_2digits(date.getMinutes()));
        time_labels.push(pad_to_2digits(date.getHours()) + ':' + pad_to_2digits(date.getMinutes()) + day_str);
        prices_out.push(Math.round(price_data.prices[idx] / 100) / 10);

        idx++;
    }

    // now history, if values exists
    if (has_import_values) {
        dataset_started = false;
        for (h_idx = net_exports.length - 1 - now_idx; h_idx < net_exports.length; h_idx++) {
            if (Math.abs(net_exports[h_idx]) > 1)
                dataset_started = true;
            imports.push(dataset_started ? -net_exports[h_idx] : null);
        }
    }
    console.log("net_exports", net_exports, "imports", imports);


    var chartExist = Chart.getChart("day_ahead_chart"); // <canvas> id
    if (chartExist != undefined)
        chartExist.destroy();

    const day_ahead_chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: time_labels,
            datasets: [{
                label: 'price ¢/kWh',
                data: prices_out,
                yAxisID: 'yp',
                borderColor: ['#2376DD'
                ],
                pointStyle: 'circle',
                pointRadius: 1,
                pointHoverRadius: 5,
                fill: false,
                stepped: true,
                borderWidth: 2
            },
            {
                label: 'import Wh',
                data: imports,
                yAxisID: 'ye',
                cubicInterpolationMode: 'monotone',
                borderColor: ['#008000'
                ],
                pointStyle: 'circle',
                pointRadius: 1,
                pointHoverRadius: 5,
                fill: false,
                stepped: false,
                borderWidth: 2
            }]
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
                        lineWidth: 6, color: "#ffffcc", borderWidth: 1, borderColor: '#f7f7e6'
                    }
                },
                yp: {
                    beginAtZero: true,
                    ticks: {
                        color: 'white',
                        font: { size: 17 }
                    },
                    position: 'right', grid: {
                        lineWidth: 1, color: "#f7f7e6", borderWidth: 1, borderColor: '#f7f7e6'
                    }
                },
                ye: {
                    display: 'auto',
                    beginAtZero: true,
                    grid: { display: false },
                    ticks: {
                        color: 'white',
                        font: { size: 17, }
                    }
                },
                x: { grid: { lineWidth: 0.5, display: true, color: "#f7f7e6" } }
            },
            interaction: {
                intersect: false,
                axis: 'x'
            },
            plugins: {
                title: {
                    display: true,
                    text: (ctx) => 'Day-ahead prices ' + start_date_str,
                    font: {
                        size: 24,
                        weight: "normal"
                    }
                },
                legend: {
                    display: false
                }
            }
        }
    });
    Chart.defaults.color = '#f7f7e6';
    Chart.defaults.scales.borderColor = '#f7f7e6';
    document.getElementById("chart_container").style.display = "block";
    return true;
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

function get_price_for_segment(start_ts, end_ts = 0) {
    if (prices.length == 0) { //prices (not yet) populated
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
        price_sum += prices[price_idx];
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



function populate_wifi_ssid_list() {
    console.log("populate_wifi_ssid_list");
    $.ajax({
        type: "GET",
        url: '/wifis',
        dataType: 'json',
        async: true,
        success: function (data) {
            wifi_ssid_list = document.getElementById('wifi_ssid_list');
            wifi_ssid_list.innerHTML = '';
            $.each(data, function (i, row) {
                console.log(row);
                addOption(wifi_ssid_list, row["id"], row["id"] + "(" + row["rssi"] + ")");
            });
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_config", textStatus, jqXHR.status, errorThrown);
        }
    });
}

//Scheduling functions

//TODO: get from main.cpp
const force_up_mins = [30, 60, 120, 180, 240, 360, 480, 600, 720, 960, 1200, 1440];



function update_schedule_select_periodical() {
    //remove schedule start times from history
    let selects = document.querySelectorAll("input[id*=':duration']");
    //sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`);
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
function post_schedule_update(channel_idx, duration, start) {
    var scheds = [];

    // live_alert(`sch_${channel_idx}`, "Updating... 'success'");

    scheds.push({ "ch_idx": channel_idx, "duration": duration, "from": start });
    console.log(scheds);

    $.ajax({
        type: "POST",
        url: "/update.schedule",
        async: "false",
        data: JSON.stringify({ schedules: scheds }),
        contentType: "application/json; charset=utf-8",
        dataType: "json",
        success: function (data) {
            sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`).value = -1;
            sch_duration_sel = document.getElementById(`sch_${channel_idx}:start`).value = -1;
            sch_duration_sel = document.getElementById(`sch_${channel_idx}:save`).disabled = false; // should not be needed
            // console.log("success", data);
            live_alert(`sch_${channel_idx}`, duration >0 ? "Schedule updated." : "Schedule deleted.", "success");

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
    update_status(false); 
   // setTimeout(function () { update_status(false); }, 2000); //update UI
}



function update_fup_schedule_element(channel_idx, current_start_ts = 0) {
    //dropdown, TODO: recalculate when new hour 
    now_ts = Date.now() / 1000;
    sch_start_sel = document.getElementById(`sch_${channel_idx}:start`);
    sch_save_radio = document.getElementById(`sch_${channel_idx}:save`);

    prev_selected = sch_start_sel.value;
    //TODO: price and so on
    $("#fupfrom_" + channel_idx).empty();
    sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`)

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
    for (k = 0; k < 24; k++) {
        end_ts = start_ts + (duration_selected * 60) - 1;
        segment_price = get_price_for_segment(start_ts, end_ts);
        if (segment_price < cheapest_price) {
            cheapest_price = segment_price;
            cheapest_ts = start_ts;
            cheapest_index = k;
        }

        if (segment_price != -VARIABLE_LONG_UNKNOWN)
            price_str = "   " + segment_price.toFixed(1) + " c/kWh";
        else
            price_str = "";

        addOption(sch_start_sel, start_ts, get_time_string_from_ts(start_ts, false, true) + "-> " + get_time_string_from_ts(start_ts + duration_selected * 60, false, true) + price_str, (prev_selected == start_ts));
        start_ts += 3600;
    }
    if (cheapest_index > -1) {
        console.log("cheapest_ts", cheapest_ts)
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
    //setTimeout(function () { update_status(false); }, 1000); //update UI
    update_status(false);
    
}

//TODO: refaktoroi myös muut
function remove_select_options(select_element) {
    for (i = select_element.options.length - 1; i >= 0; i--) {
        select_element.remove(i);
    }
}


//End of scheduling functions

function load_config() {
    //current config
    $.ajax({
        type: "GET",
        url: '/settings', ///data/arska-config.json',
        dataType: 'json',
        async: false,
        success: function (data) { g_config = data; console.log("got g_config"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_config", textStatus, jqXHR.status, errorThrown);
        }
    });

    $.ajax({
        url: '/data/arska-mappings.json',
        dataType: 'json',
        async: false,
        success: function (data) { g_datamapping = data; console.log("got g_datamapping"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_datamapping", textStatus, jqXHR.status, errorThrown);
        }
    });

    for (const property in g_config) {
        //  console.log(`${property}: ${g_config[property]}`);
        if (property in g_datamapping) {
            // console.log(g_datamapping[property][0]);  
            field_name = g_datamapping[property][0];
            //   console.log(field_name + "#");
            ctrl = document.getElementById(field_name);
            if (ctrl !== null) {
                if (ctrl.type.toLowerCase() == 'checkbox')
                    ctrl.checked = g_config[property];
                else // normal text
                    ctrl.value = g_config[property];
            }
        }
    }


};

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

    /*

    document.getElementById(`ch_${channel_idx}:r_ip_div`).style.display = is_relay_ip_used(ch_type) ? "segment" : "none";
    document.getElementById("d_rid_" + channel_idx).style.display = is_relay_id_used(ch_type) ? "segment" : "none";
    document.getElementById("d_ruid_" + channel_idx).style.display = is_relay_uid_used(ch_type) ? "segment" : "none";
    relay_id_caption_span = document.getElementById("ch_ridcap_" + channel_idx);

    if (is_relay_id_used(ch_type)) {
        max_rid = ([CH_TYPE_GPIO_USER_DEF, CH_TYPE_GPIO_USR_INVERSED].includes(parseInt(ch_type)) ? 39 : 255); //GPIO max 39
        document.getElementById("ch_rid_" + channel_idx).setAttribute("max", max_rid);
    }
    if (is_relay_uid_used(ch_type)) {
        min_ruid = (ch_type == (CH_TYPE_TASMOTA) ? 1 : 0); //Shelly min 0, Tasmota 1
        cur_ruid = document.getElementById("ch_ruid_" + channel_idx).value;
        document.getElementById("ch_ruid_" + channel_idx).setAttribute("min", min_ruid);
        document.getElementById("ch_ruid_" + channel_idx).value = Math.max(min_ruid, cur_ruid); //for min value
    }

    if ([CH_TYPE_GPIO_USER_DEF, CH_TYPE_GPIO_USR_INVERSED].includes(parseInt(chtype))) {
        relay_id_caption_span.innerHTML = "gpio:<br>";
    }
    else if (chtype == CH_TYPE_GPIO_FIXED) {
        relay_id_caption_span.innerHTML = "gpio (fixed):<br>";
        $('#ch_rid_' + channel_idx).attr('disabled', true);
    }
    else
        relay_id_caption_span.innerHTML = "device id:"; //TODO: later modbus...
    
    */
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

function get_template_list() {
    if (g_template_list.length > 0)
        return;

    $.ajax({
        url: '/data/templates.json',
        dataType: 'json',
        async: false,
        success: function (data) {
            $.each(data, function (i, row) {
                g_template_list.push({ "id": row["id"], "name": _ltext(row, "name") });
            });
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get templates", textStatus, jqXHR.status);
        }
    });
}


function populateTemplateSel(selEl, template_id = -1) {
    // console.log("populateTemplateSel");
    get_template_list();
    if (selEl.options && selEl.options.length > 1) {
        console.log("already populated", selEl.options.length);
        return; //already populated
    }
    addOption(selEl, -1, "Select template", false);
    // console.log("g_template_list.length", g_template_list.length);
    for (i = 0; i < g_template_list.length; i++) {
        addOption(selEl, g_template_list[i]["id"], g_template_list[i]["id"] + " - " + g_template_list[i]["name"], (template_id == g_template_list[i]["id"]));
    }
    /*
    $.getJSON('/data/template-list.json', function (data) {
        $.each(data, function (i, row) {
            addOption(selEl, row["id"], row["id"] + " - " + _ltext(row, "name"), (template_id == row["id"]));
        });
    });*/
}
//KESKEN
function switch_rule_mode(channel_idx, rule_mode, reset, template_id) {
    template_id_ctrl = document.getElementById(`ch_${channel_idx}:template_id`);
    if (rule_mode == CHANNEL_CONFIG_MODE_TEMPLATE) { //template mode
        if (template_id_ctrl) {
            populateTemplateSel(template_id_ctrl, template_id);
            if (reset)
                template_id_ctrl.value = -1;
        }
        else
            console.log("Cannot find element:", template_id_ctrl);
    }

    rules_div = document.getElementById(`ch_${channel_idx}:rules`);

    $(`#ch_${channel_idx}\\:rules select`).attr('disabled', (rule_mode != 0));
    $(`#ch_${channel_idx}\\:rules input[type='text']`).attr('disabled', (rule_mode != 0)); //jos ei iteroi?
    $(`#ch_${channel_idx}\\:rules input[type='text']`).prop('readonly', (rule_mode != 0));

    //$('#rts_' + channel_idx).css({ "display": ((rule_mode == CHANNEL_CONFIG_MODE_RULE) ? "none" : "segment") });
    template_id_ctrl.disabled = (rule_mode == CHANNEL_CONFIG_MODE_RULE);

    //  $('#rtr_' + channel_idx).css({ "display": ((rule_mode == CHANNEL_CONFIG_MODE_RULE) ? "none" : "segment") });
    template_reset_ctrl = document.getElementById(`ch_${channel_idx}:template_reset`);
    template_reset_ctrl.disabled = (rule_mode == CHANNEL_CONFIG_MODE_RULE);

    // seuraava kesken
    // fillStmtRules(channel_idx, rule_mode, template_id);
    // if (rule_mode == CHANNEL_CONFIG_MODE_RULE) //enable all
    //     $('#rd_' + channel_idx + " input[type='radio']").attr('disabled', false);
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

    console.log("populateStmtField", channel_idx, rule_idx, stmt_idx, stmt);

    // assume populateted...


    var_ctrl = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`);
    var_this = get_var_by_id(stmt[0]);
    populate_var(var_ctrl, stmt[0]);

    oper_ctrl = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:oper`);
    populate_oper(oper_ctrl, var_this, stmt);


    var_ctrl.value = stmt[0];
    oper_ctrl.value = stmt[1];
    document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`).value = stmt[3];



    // document.getElementById(varFld.id.replace("var", "const")).style.display = "none"; //const-style
    // document.getElementById(varFld.id.replace("var", "op")).style.display = "none";

    //    advanced_mode = document.getElementById("mo_" + channel_idx + "_0").checked;

    //  addOption(varFld, -2, "delete condition");
    // addOption(varFld, -1, "select", (stmt[0] == -1));
    /*
        for (var i = 0; i < g_constants.variables.length; i++) {
            var type_indi = (g_constants.variables[i][2] >= 50 && g_constants.variables[i][2] <= 51) ? "*" : " "; //logical
            var id_str = '(' + g_constants.variables[i][0] + ') ' + g_constants.variables[i][1] + type_indi;
            addOption(varFld, g_constants.variables[i][0], id_str, (stmt[0] == g_constants.variables[i][0]));
        }
        */
}

function addStmt(channel_idx, rule_idx = 1, stmt_idx = -1, stmt = [-1, -1, 0, 0]) {
    //get next statement index if not defined
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
function template_reset(ev) {
    channel_idx = get_idx_from_str(ev.target.id, 0);

    console.log(ev, "---", ev.target.id, "template_reset channel_idx", channel_idx);
    set_template_constants(channel_idx, false);
}

function set_template_constants(channel_idx, ask_confirmation) {
    template_idx = document.getElementById(`ch_${channel_idx}:template_id`).value;

    url = '/data/templates.json';
    if (template_idx == -1) {
        if (confirm('Remove template definitions')) {
            delete_stmts_from_UI(channel_idx);
            return true;
        }
        else {
            $sel.val($sel.data('current')); //back current
            return false;
        }
    }

    delete_stmts_from_UI(channel_idx);
    var template_data;
    $.ajax({
        url: url,
        dataType: 'json',
        async: false,
        success: function (data) {
            $.each(data, function (i, row) {
                if (row["id"] == template_idx) {
                    template_data = row;
                    return;
                }
            });
        },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get template", textStatus, jqXHR.status);
        }
    });

    console.log("template_data", template_data);
    $sel = $("#ch_" + channel_idx + "\\:template_id");

    if (!confirm('Use template ' + _ltext(template_data, "name") + " \n" + _ltext(template_data, "desc"))) {
        console.log("set back to", $sel.val(), "->", $sel.data('current'));
        $sel.val($sel.data('current')); //back current
        return false;
    }

    $sel.data('current', $sel.val()); // now fix current value

    delete_stmts_from_UI(channel_idx);

    $.each(template_data.conditions, function (cond_idx, rule) {
        // set_radiob("ctrb_" + channel_idx + "_" + cond_idx, rule["on"] ? "1" : "0", true);

        document.getElementById(`ch_${channel_idx}:r_${cond_idx}:up_0`).checked = !rule["on"];
        document.getElementById(`ch_${channel_idx}:r_${cond_idx}:up_1`).checked = rule["on"];
        $.each(rule.statements, function (j, stmt) {
            //   console.log("stmt.values:" + JSON.stringify(stmt.values));
            stmt_obj = stmt.values;
            if (stmt.hasOwnProperty('const_prompt')) {
                stmt_obj[3] = prompt(stmt.const_prompt, stmt_obj[2]);
            }
            addStmt(channel_idx, cond_idx, -1, stmt_obj);

            //    if (elBtn) {
            //        addStmt(elBtn, channel_idx, cond_idx, j, stmt_obj);
            //    }
            var_this = get_var_by_id(stmt.values[0]);
            //   populateOper(document.getElementById("op_" + channel_idx + "_" + cond_idx + "_" + j), var_this, stmt_obj);
        });
    });


    // fillStmtRules(channel_idx, 1, template_idx);
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
  //  console.log("populate_channel", channel_idx);

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

    //experimental, is this enough or do we need loop
    //  update_fup_duration_element(channel_idx, current_duration_minute, has_forced_setting);
    update_fup_schedule_element(channel_idx, current_start_ts);
    /////sch_(ch#):card
    document.getElementById(`sch_${channel_idx}:card`).style.display = ((g_config.ch[channel_idx]["type"] == 0) ? "none" : "block");

    // end of scheduling

    document.getElementById(`ch_${channel_idx}:id_str`).value = g_config.ch[channel_idx]["id_str"];
    document.getElementById(`sch_${channel_idx}:title`).innerText = g_config.ch[channel_idx]["id_str"];

    document.getElementById(`ch_${channel_idx}:uptime_minimum_m`).value = parseInt(g_config.ch[channel_idx]["uptime_minimum"] / 60);

    // console.log("channel_color", channel_idx, (g_config.ch[channel_idx]["channel_color"]), g_config.ch[channel_idx]["channel_color"]);
    document.getElementById(`ch_${channel_idx}:channel_color`).value = (g_config.ch[channel_idx]["channel_color"]);

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
                /*    document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`).value = this_stmt[0];
                    document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:oper`).value = this_stmt[1];
                    document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`).value = this_stmt[3];
            */
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

    const_ctrl = document.getElementById(ev.target.id.replace("var", "const"));
    el_const.style.display = (is_var_logical(var_this[2])) ? "none" : "segment"; //const-style




    //    if (g_constants.opers[i][0] == stmt[1]) {
    //      el_const.style.display = (g_constants.opers[i][5] || g_constants.opers[i][6]) ? "none" : "segment"; //const-style
    //  }

}

// operator select changed, show next statement fields if hidden
function selected_oper(ev) {
    const el_oper = ev.target;
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
        console.log("show_const params:", g_constants.opers[el_oper.value][5], g_constants.opers[el_oper.value][6])
        show_const = !(g_constants.opers[el_oper.value][5] || g_constants.opers[el_oper.value][6]);
        //   el_const.style.display = (g_constants.opers[el_oper.value][5] || g_constants.opers[el_oper.value][6]) ? "none" : "segment"; // const-style
    }

    el_const.classList.remove(show_const ? "invisible" : "visible");
    el_const.classList.add(show_const ? "visible" : "invisible");

}

function populate_var(sel_ctrl, selected = -1) {
    // get_idx_from_str(sel_ctrl.id)
    if (sel_ctrl.options.length == 0) {
        addOption(sel_ctrl, -1, "", false);
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

    if (el_oper.options.length == 0) {
        el_oper.addEventListener("change", selected_oper);
    }

    // rule_mode = document.getElementById("mo_" + channel_idx + "_0").checked ? 0 : 1;
    // rule_mode = 0; //TODO: FIX
    rule_mode = document.getElementById(`ch_${channel_idx}:config_mode_0`).checked ? 0 : 1;


    el_var = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:var`);
    el_const = document.getElementById(`ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`);
    console.log(el_oper.id, `ch_${channel_idx}:r_${rule_idx}:s_${stmt_idx}:const`);

    el_const.value = stmt[2];

    if (el_oper.options.length == 0) {
        addOption(el_oper, -1, "", (stmt[1] == -1));
    }
    if (el_oper.options) {
        while (el_oper.options.length > 1) {
            el_oper.remove(1);
        }
    }

    console.log(var_this);
    var show_constant = false;
    if (var_this) {
        //populate oper select
        console.log("populate oper select, length ", g_constants.opers.length);
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
    el_const.classList.remove(show_constant ? "invisible" : "visible");
    el_const.classList.add(show_constant ? "visible" : "invisible");

    el_const.classList.disabled = !show_constant;


    el_oper.disabled = (rule_mode != 0);
    el_var.disabled = (rule_mode != 0);
    el_const.disabled = (rule_mode != 0);
}



function focus_oper(ev) {
    sel_ctrl = ev.target;
    // populate_oper(sel_ctrl);

}

function create_channels() {
    console.log("create_channels");

    //front page 
    // for (channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) { //
    //     document.getElementById("schedules").insertAdjacentHTML('beforeend', schedule_html.replaceAll("sch_(ch#)", "sch_" + channel_idx));
    // }
    for (channel_idx = g_constants.CHANNEL_COUNT - 1; channel_idx > -1; channel_idx--) { //beforeend
        document.getElementById("schedules").insertAdjacentHTML('afterbegin', schedule_html.replaceAll("sch_(ch#)", "sch_" + channel_idx).replaceAll("(ch#)", channel_idx));
    }


    // channel settings page
    channels = document.getElementById("channels");
    for (channel_idx = 0; channel_idx < g_constants.CHANNEL_COUNT; channel_idx++) {
        channels.insertAdjacentHTML('beforeend', channel_html.replaceAll("ch_#", "ch_" + channel_idx));

        // populate types, "channel_types": [
        //    `ch_${channel_idx}:type`
        channel_type_ctrl = document.getElementById(`ch_${channel_idx}:type`);
        //  console.log(`ch_${channel_idx}:type`);
        for (var i = 0; i < g_constants.channel_types.length; i++) {
            addOption(channel_type_ctrl, g_constants.channel_types[i].id, g_constants.channel_types[i].name, (g_config.ch[channel_idx]["type"] == g_constants.channel_types[i].id));
        }

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
                    //       console.log(`${stmt_id}:var`);
                    document.getElementById(`${stmt_id}:var`).addEventListener("focus", focus_var);
                    document.getElementById(`${stmt_id}:oper`).addEventListener("focus", focus_oper);
                }
            }

            // schedule controls
            sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`);
            remove_select_options(sch_duration_sel);
            addOption(sch_duration_sel, -1, "duration", true); //check checked
            for (i = 0; i < force_up_mins.length; i++) {
                min_cur = force_up_mins[i];
                duration_str = pad_to_2digits(parseInt(min_cur / 60)) + ":" + pad_to_2digits(parseInt(min_cur % 60));
                addOption(sch_duration_sel, min_cur, duration_str, false); //check checked
            }

            // populate data
            populate_channel(channel_idx);
        }
        feather.replace(); // this replaces  <span data-feather="activity">  with svg

        //Schedulings listeners
        sch_duration_sel = document.getElementById(`sch_${channel_idx}:duration`);
        sch_duration_sel.addEventListener("change", duration_changed);

        sch_duration_sel = document.getElementById(`sch_${channel_idx}:delete`);
        sch_duration_sel.addEventListener("click", delete_schedule);


        channel_type_ctrl.addEventListener("change", set_channel_fields_by_type);

        // refine template constants

        template_reset_ctrl = document.getElementById(`ch_${channel_idx}:template_reset`);
        template_reset_ctrl.addEventListener("click", template_reset);

        document.getElementById(`ch_${channel_idx}:template_id`).addEventListener("change", changed_template);

    }
    let cm_buttons = document.querySelectorAll("input[id*=':config_mode_']");

    console.log(cm_buttons.length, " buttons found!");
    for (let i = 0; i < cm_buttons.length; i++) {
        // console.log(cm_buttons[i].id, "changed_rule_mode");
        cm_buttons[i].addEventListener("click", changed_rule_mode);
    }

}

function init_ui() {
    console.log("init_ui");
    get_price_data(); //TODO: also update at 2pm
    $.ajax({
        url: '/application',
        dataType: 'json',
        async: false,
        success: function (data) { g_constants = data; console.log("got g_constants"); },
        error: function (jqXHR, textStatus, errorThrown) {
            console.log("Cannot get g_constants", textStatus, jqXHR.status);
        }
    });

    load_config();

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


    action_buttons = document.querySelectorAll("button[id^='actions:']");
    for (let i = 0; i < action_buttons.length; i++) {
        action_buttons[i].addEventListener("click", launch_action);
        //  console.log("Action added " + action_buttons[i].id);
    }

    const input_controls = document.querySelectorAll("input, select");
    for (let i = 0; i < input_controls.length; i++) {
        if (input_controls[i].type == "radio")
            input_controls[i].addEventListener("click", ctrl_changed);
        else
            input_controls[i].addEventListener("change", ctrl_changed);
        // console.log("Action added " + input_controls[i].id);
    }

    update_price_chart();//TODO: timing, refresh, synch with update_status
    setTimeout(function () { update_price_chart; }, 60);
    // populate_wifi_ssid_list();
    update_status(true);
}

function live_alert(card_idstr, message, type) {
    alertPlaceholder = document.getElementById(card_idstr + ':alert');
    if (alertPlaceholder !== null) {
        const wrapper = document.createElement('div');
        wrapper.innerHTML = [
            `<div class="alert alert-${type} alert-dismissible" role="alert">`,
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
// <div class="card  mb-3" id="price_data:card">
function find_parent_card(el) {
    var p = el;
    while (p = p.parentNode) {
        if (p.id && p.id.endsWith(":card"))
            return p.id.replace(":card", "");
    }
    return "";
}

function ctrl_changed(ev) {
    /* id_a = ev.target.id.split(":");
     if (len(id_a) != 2) // not in settings
         return;*/
    let parent_card = find_parent_card(ev.target);


    if (parent_card) {
        // if (parent_card.startsWith("sch_")) // no save buttons so far...
        //     return;
        // console.log("Found " + ev.target.id + " in parent " + parent_card + ", disabling " + parent_card + ":save");
        document.getElementById(parent_card + ":save").disabled = false;
    }
}

function launch_action(ev) {
    id_a = ev.target.id.split(":");
    card = id_a[0]; // should be "action"
    action = id_a[1];
    var post_data = { "action": action };

    //card_div = document.getElementById(card + ":card");

    live_alert(card, "Sending:\n" + JSON.stringify(post_data), 'success');
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

    data_ch["channel_color"] = document.getElementById(`ch_${channel_idx}:channel_color`).value;

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
                console.log("A", [var_value, oper_value, null, const_value])
                stmt_count++;
            }
        }
        if (rule_stmts.length > 0)
            rules.push({ "on": up_value, "stmts": rule_stmts });
    }
    data_ch["rules"] = rules;


    const post_data = { "ch": [data_ch] };


    //POST

    live_alert(card, "Updating... 'success'");


    $.ajax({
        type: "POST",
        url: "/settings",
        async: "false",
        data: JSON.stringify(post_data),
        contentType: "application/json; charset=utf-8",
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



function save_card(ev) {
    id_a = ev.target.id.split(":");
    card = id_a[0];
    var post_data = {};

    card_div = document.getElementById(card + ":card");

    let elems = card_div.querySelectorAll('input, select');
    for (let i = 0; i < elems.length; i++) {
        console.log("POST:" + elems[i].id + ", " + elems[i].type.toLowerCase())
        if (elems[i].type.toLowerCase() == 'checkbox') {
            post_data[elems[i].id] = elems[i].checked;
        }
        else {
            post_data[elems[i].id] = elems[i].value;
        }
    }


    // alert("Sending:\n" + JSON.stringify(post_data));

    live_alert(card, "Response comes here. Sending:\n" + JSON.stringify(post_data), 'success');

    $.ajax({
        type: "POST",
        url: "/settings",
        async: "false",
        data: JSON.stringify(post_data),
        contentType: "application/json; charset=utf-8",
        dataType: "json",
        success: function (data) {
            live_alert(card, "Updated", 'success');
            console.log("success, card ", card, data);
            document.getElementById(card + ":save").disabled = true;
            console.log("save_channel save disabled");
        },
        error: function (requestObject, error, errorThrown) {
            console.log(error, errorThrown);
            live_alert(card, "Update failed: " + error + ", " + errorThrown, 'warning');
        }
    });




}

