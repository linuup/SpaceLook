// 在根元素添加
property var previewData: ({
    "typeKey": "text",
    "typeLabel": "Text File",
    "sourceKind": "Local File",
    "title": "example.txt",
    "statusMessage": "Ready to preview",
    "filePath": "C:/example.txt",
    "fileSize": "1.2 KB",
    "modifiedTime": "2024-01-15 10:30"
})

// 创建与 C++ 的接口
QtObject {
    id: bridge
    function updatePreview(data) {
        root.previewData = data
    }
}