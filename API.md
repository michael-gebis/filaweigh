## REST API documentation
- Adopted from: https://gist.github.com/azagniotov/a4b16faf0febd12efbc6c3d7370383a6#file-beautiful-rest-api-docs-in-markdown-md

<details>
 <summary><code>GET</code> <code><b>/API/v1/scale</b></code> <code>Gets current state of the scale</code></summary>

##### Parameters

> | name      |  type     | data type               | description                                                           |
> |-----------|-----------|-------------------------|-----------------------------------------------------------------------|
> | None      |  required | object (JSON or YAML)   | N/A  |


##### Responses

> | http code     | content-type                      | response                                                            |
> |---------------|-----------------------------------|---------------------------------------------------------------------|
> | `200`         | `text/plain;charset=UTF-8`        | `{"weight":"1021234", "tare":"97348" }`                                |

##### Example cURL

> ```javascript
>  curl -X POST -H "Content-Type: application/json" --data @post.json http://localhost/API/v1/scale/
> ```

</details>
