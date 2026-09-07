const taskForm = document.getElementById("task-form");
const taskInput = document.getElementById("task-input");
const taskList = document.getElementById("task-list");
const emptyState = document.getElementById("empty-state");
const taskCount = document.getElementById("task-count");


async function loadTasks() {
    try {
        const response = await fetch("/tasks");

        if(!response.ok) {
            throw new Error("Failed to load tasks");
        }

        const tasks = await response.json();

        renderTasks(tasks);
    } catch(error) {
        console.error(error);
    }
}


function renderTasks(tasks) {
    taskList.innerHTML = "";

    const completedCount = tasks.filter(
        task => task.completed
    ).length;

    if(tasks.length === 0) {
        emptyState.style.display = "flex";
    } else {
        emptyState.style.display = "none";
    }

    if(tasks.length === 1) {
        taskCount.textContent = "1 task";
    } else {
        taskCount.textContent = `${tasks.length} tasks`;
    }

    for(const task of tasks) {
        const element = createTaskElement(task);

        taskList.appendChild(element);
    }
}


function createTaskElement(task) {
    const element = document.createElement("div");

    element.className = "task";

    element.innerHTML = `
        <button class="task-checkbox ${task.completed ? "completed" : ""}" aria-label="Toggle task"></button>

        <div class="task-title ${task.completed ? "completed" : ""}">
            ${escapeHtml(task.title)}
        </div>

        <div class="task-actions">

            <button class="task-action edit" title="Edit task">
                Edit
            </button>

            <button class="task-action delete" title="Delete task">
                ×
            </button>

        </div>
    `;

    const checkbox = element.querySelector(".task-checkbox");
    const editButton = element.querySelector(".edit");
    const deleteButton = element.querySelector(".delete");

    checkbox.addEventListener("click", () => {
        toggleTask(task);
    });

    editButton.addEventListener("click", () => {
        editTask(task);
    });

    deleteButton.addEventListener("click", () => {
        deleteTask(task.id);
    });

    return element;
}


async function createTask(title) {
    const response = await fetch("/tasks", {
        method: "POST",

        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },

        body: `title=${encodeURIComponent(title)}`
    });

    if(!response.ok) {
        throw new Error("Failed to create task");
    }

    await loadTasks();
}


async function toggleTask(task) {
    const response = await fetch(`/tasks/${task.id}`, {
        method: "PUT",

        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },

        body:
            `title=${encodeURIComponent(task.title)}` +
            `&completed=${!task.completed}`
    });

    if(!response.ok) {
        console.error("Failed to update task");
        return;
    }

    await loadTasks();
}


async function editTask(task) {
    const title = window.prompt(
        "Edit task",
        task.title
    );

    if(title === null) {
        return;
    }

    const trimmedTitle = title.trim();

    if(trimmedTitle === "") {
        return;
    }

    const response = await fetch(`/tasks/${task.id}`, {
        method: "PUT",

        headers: {
            "Content-Type": "application/x-www-form-urlencoded"
        },

        body:
            `title=${encodeURIComponent(trimmedTitle)}` +
            `&completed=${task.completed}`
    });

    if(!response.ok) {
        console.error("Failed to edit task");
        return;
    }

    await loadTasks();
}


async function deleteTask(id) {
    const confirmed = window.confirm(
        "Delete this task?"
    );

    if(!confirmed) {
        return;
    }

    const response = await fetch(`/tasks/${id}`, {
        method: "DELETE"
    });

    if(!response.ok && response.status !== 204) {
        console.error("Failed to delete task");
        return;
    }

    await loadTasks();
}


function escapeHtml(value) {
    return value
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#039;");
}


taskForm.addEventListener("submit", async (event) => {
    event.preventDefault();

    const title = taskInput.value.trim();

    if(title === "") {
        taskInput.focus();
        return;
    }

    const button = taskForm.querySelector("button");

    button.disabled = true;
    button.textContent = "Adding...";

    try {
        await createTask(title);

        taskInput.value = "";
        taskInput.focus();
    } catch(error) {
        console.error(error);
    } finally {
        button.disabled = false;
        button.textContent = "Add task";
    }
});


loadTasks();