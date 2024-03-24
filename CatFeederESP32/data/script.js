async function populateTable(){
    const timesTable = document.getElementById("timesTable");
    const response = await fetch("feedTimes.json");
    const feedTimes = await response.json();

    for (i in feedTimes["times"]){
        const id = feedTimes["times"][i]["id"];

        //delete
        const deleteForm = document.createElement("form");
        deleteForm.action = "/deleteRow";
        deleteForm.method = "post";
        const inputHidden = document.createElement("input");
        inputHidden.type = "hidden";
        inputHidden.name = "id";
        inputHidden.value = id;
        const inputSubmit = document.createElement("input");
        inputSubmit.type = "submit";
        inputSubmit.value = "Delete";
        deleteForm.appendChild(inputHidden);
        deleteForm.appendChild(inputSubmit);

        //table
        const tableRow = document.createElement("tr");
        const tableCellTime = document.createElement("td");
        const tableCellTimesFed = document.createElement("td");
        const tableCellDelete = document.createElement("td");

        const timeNode = document.createTextNode(feedTimes["times"][i]["time"]);
        const timesFedNode = document.createTextNode(feedTimes["times"][i]["cycles"]);

        tableCellTime.appendChild(timeNode);
        tableCellDelete.append(deleteForm);
        tableCellTimesFed.append(timesFedNode);

        tableRow.appendChild(tableCellTime);
        tableRow.appendChild(tableCellTimesFed);
        tableRow.appendChild(tableCellDelete);
        timesTable.appendChild(tableRow);
    }
}